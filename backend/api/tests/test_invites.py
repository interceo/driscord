"""Invite lifecycle: create, list, revoke, accept.

Invites were previously exercised only as a helper inside the server tests;
this file pins the whole surface of routers/invites.py.
"""


async def _make_server(client, headers) -> int:
    r = await client.post("/servers/", json={"name": "srv"}, headers=headers)
    assert r.status_code == 201, r.text
    return r.json()["id"]


async def _invite_member(client, owner_headers, member_headers, server_id: int):
    invite = await client.post(
        f"/servers/{server_id}/invites/", headers=owner_headers
    )
    assert invite.status_code == 201, invite.text
    code = invite.json()["code"]
    joined = await client.post(f"/invites/{code}", headers=member_headers)
    assert joined.status_code == 200, joined.text
    return code


async def test_member_creates_invite(client, auth_headers):
    headers = await auth_headers("alice")
    server_id = await _make_server(client, headers)

    r = await client.post(f"/servers/{server_id}/invites/", headers=headers)
    assert r.status_code == 201
    body = r.json()
    assert len(body["code"]) == 8
    assert body["server_id"] == server_id


async def test_non_member_cannot_create_invite(client, auth_headers):
    owner = await auth_headers("alice")
    outsider = await auth_headers("mallory")
    server_id = await _make_server(client, owner)

    r = await client.post(f"/servers/{server_id}/invites/", headers=outsider)
    assert r.status_code == 403


async def test_invite_for_unknown_server_is_404(client, auth_headers):
    headers = await auth_headers("alice")
    r = await client.post("/servers/424242/invites/", headers=headers)
    assert r.status_code == 404


async def test_only_owner_lists_invites(client, auth_headers):
    owner = await auth_headers("alice")
    member = await auth_headers("bob")
    server_id = await _make_server(client, owner)
    code = await _invite_member(client, owner, member, server_id)

    listed = await client.get(f"/servers/{server_id}/invites/", headers=owner)
    assert listed.status_code == 200
    assert code in [i["code"] for i in listed.json()]

    refused = await client.get(f"/servers/{server_id}/invites/", headers=member)
    assert refused.status_code == 403


async def test_accept_invite_joins_then_reports_already_member(client, auth_headers):
    owner = await auth_headers("alice")
    joiner = await auth_headers("bob")
    server_id = await _make_server(client, owner)

    invite = await client.post(f"/servers/{server_id}/invites/", headers=owner)
    code = invite.json()["code"]

    first = await client.post(f"/invites/{code}", headers=joiner)
    assert first.status_code == 200
    assert first.json() == {"server_id": server_id, "status": "joined"}

    second = await client.post(f"/invites/{code}", headers=joiner)
    assert second.status_code == 200
    assert second.json()["status"] == "already_member"


async def test_accept_unknown_code_is_404(client, auth_headers):
    headers = await auth_headers("alice")
    r = await client.post("/invites/nosuchcd", headers=headers)
    assert r.status_code == 404


async def test_owner_revokes_invite_and_code_stops_working(client, auth_headers):
    owner = await auth_headers("alice")
    joiner = await auth_headers("bob")
    server_id = await _make_server(client, owner)

    invite = await client.post(f"/servers/{server_id}/invites/", headers=owner)
    code = invite.json()["code"]

    revoked = await client.delete(
        f"/servers/{server_id}/invites/{code}", headers=owner
    )
    assert revoked.status_code == 204

    r = await client.post(f"/invites/{code}", headers=joiner)
    assert r.status_code == 404


async def test_creator_can_revoke_own_invite(client, auth_headers):
    owner = await auth_headers("alice")
    member = await auth_headers("bob")
    server_id = await _make_server(client, owner)
    await _invite_member(client, owner, member, server_id)

    invite = await client.post(f"/servers/{server_id}/invites/", headers=member)
    code = invite.json()["code"]

    revoked = await client.delete(
        f"/servers/{server_id}/invites/{code}", headers=member
    )
    assert revoked.status_code == 204


async def test_plain_member_cannot_revoke_someone_elses_invite(client, auth_headers):
    owner = await auth_headers("alice")
    member = await auth_headers("bob")
    server_id = await _make_server(client, owner)
    await _invite_member(client, owner, member, server_id)

    invite = await client.post(f"/servers/{server_id}/invites/", headers=owner)
    code = invite.json()["code"]

    refused = await client.delete(
        f"/servers/{server_id}/invites/{code}", headers=member
    )
    assert refused.status_code == 403


async def test_revoke_unknown_invite_is_404(client, auth_headers):
    owner = await auth_headers("alice")
    server_id = await _make_server(client, owner)

    r = await client.delete(f"/servers/{server_id}/invites/nosuchcd", headers=owner)
    assert r.status_code == 404


async def test_invite_endpoints_require_auth(client, auth_headers):
    owner = await auth_headers("alice")
    server_id = await _make_server(client, owner)
    invite = await client.post(f"/servers/{server_id}/invites/", headers=owner)
    code = invite.json()["code"]

    for method, url in [
        ("post", f"/servers/{server_id}/invites/"),
        ("get", f"/servers/{server_id}/invites/"),
        ("delete", f"/servers/{server_id}/invites/{code}"),
        ("post", f"/invites/{code}"),
    ]:
        r = await getattr(client, method)(url)
        assert r.status_code in (401, 403), (method, url, r.status_code)
