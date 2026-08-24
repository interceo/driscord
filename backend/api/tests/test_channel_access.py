"""Tests for /channels/{channel_id}/access — the SFU authorization contract.

The signaling server calls this endpoint for every WebSocket upgrade
(ApiAuthenticator). Its C++ side is tested only against FakeApiServer, a stub
of this router, so this file is the other half of that contract: the real
router must produce exactly the statuses and identity payload the SFU parses.
"""
from datetime import datetime, timedelta, timezone

from jose import jwt

from config import settings
from security import ALGORITHM


async def _make_server_and_channel(client, headers) -> tuple[int, int]:
    server = await client.post("/servers/", json={"name": "srv"}, headers=headers)
    assert server.status_code == 201, server.text
    server_id = server.json()["id"]
    channel = await client.post(
        f"/servers/{server_id}/channels/",
        json={"name": "voice"},
        headers=headers,
    )
    assert channel.status_code == 201, channel.text
    return server_id, channel.json()["id"]


async def test_member_is_authorized_with_identity_payload(client, auth_headers):
    headers = await auth_headers("alice")
    server_id, channel_id = await _make_server_and_channel(client, headers)

    r = await client.get(f"/channels/{channel_id}/access", headers=headers)
    assert r.status_code == 200, r.text
    body = r.json()
    # The SFU parses every one of these fields; renaming any is a breaking
    # change for backend/signaling_server/src/api_authenticator.cpp.
    assert body["channel_id"] == channel_id
    assert body["server_id"] == server_id
    assert body["user_id"] > 0
    assert body["username"] == "alice"
    assert "display_name" in body


async def test_non_member_is_refused(client, auth_headers):
    owner = await auth_headers("alice")
    outsider = await auth_headers("mallory")
    _, channel_id = await _make_server_and_channel(client, owner)

    r = await client.get(f"/channels/{channel_id}/access", headers=outsider)
    assert r.status_code == 403


async def test_unknown_channel_is_404(client, auth_headers):
    headers = await auth_headers("alice")
    r = await client.get("/channels/424242/access", headers=headers)
    assert r.status_code == 404


async def test_missing_token_is_refused(client, auth_headers):
    headers = await auth_headers("alice")
    _, channel_id = await _make_server_and_channel(client, headers)

    r = await client.get(f"/channels/{channel_id}/access")
    assert r.status_code in (401, 403)


async def test_garbage_token_is_refused(client, auth_headers):
    headers = await auth_headers("alice")
    _, channel_id = await _make_server_and_channel(client, headers)

    r = await client.get(
        f"/channels/{channel_id}/access",
        headers={"Authorization": "Bearer not-a-jwt"},
    )
    assert r.status_code == 401


async def test_expired_token_is_refused(client, auth_headers):
    headers = await auth_headers("alice")
    _, channel_id = await _make_server_and_channel(client, headers)

    expired = jwt.encode(
        {
            "sub": "1",
            "exp": datetime.now(timezone.utc) - timedelta(minutes=1),
            "type": "access",
        },
        settings.secret_key,
        algorithm=ALGORITHM,
    )
    r = await client.get(
        f"/channels/{channel_id}/access",
        headers={"Authorization": f"Bearer {expired}"},
    )
    assert r.status_code == 401


async def test_refresh_token_cannot_authorize_media(client, register, auth_headers):
    headers = await auth_headers("alice")
    _, channel_id = await _make_server_and_channel(client, headers)

    tokens = await register("bob")
    r = await client.get(
        f"/channels/{channel_id}/access",
        headers={"Authorization": f"Bearer {tokens['refresh']}"},
    )
    assert r.status_code == 401
