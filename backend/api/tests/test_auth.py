from jose import jwt

from config import settings
from security import ALGORITHM, hash_password, verify_password


def test_bcrypt_does_not_accept_a_longer_password_with_the_same_prefix():
    password = "a" * 72
    hashed = hash_password(password)
    assert verify_password(password, hashed)
    assert not verify_password(password + "b", hashed)


async def test_register_returns_tokens(client):
    r = await client.post(
        "/auth/register",
        json={"username": "alice", "email": "alice@example.com", "password": "pw12345"},
    )
    assert r.status_code == 201
    data = r.json()
    assert data["access_token"]
    assert data["refresh_token"]
    assert data["token_type"] == "bearer"


async def test_register_rejects_password_over_bcrypt_limit(client):
    r = await client.post(
        "/auth/register",
        json={
            "username": "alice",
            "email": "alice@example.com",
            "password": "я" * 36 + "x",
        },
    )
    assert r.status_code == 422


async def test_register_duplicate_username(client):
    body = {"username": "alice", "email": "alice@example.com", "password": "pw12345"}
    r = await client.post("/auth/register", json=body)
    assert r.status_code == 201
    body2 = {"username": "alice", "email": "other@example.com", "password": "pw12345"}
    r = await client.post("/auth/register", json=body2)
    assert r.status_code == 409
    assert "Username" in r.json()["detail"]


async def test_register_duplicate_email(client):
    r = await client.post(
        "/auth/register",
        json={"username": "alice", "email": "dup@example.com", "password": "pw12345"},
    )
    assert r.status_code == 201
    r = await client.post(
        "/auth/register",
        json={"username": "bob", "email": "dup@example.com", "password": "pw12345"},
    )
    assert r.status_code == 409
    assert "Email" in r.json()["detail"]


async def test_login_success(client, register):
    await register("alice")
    r = await client.post("/auth/login", json={"username": "alice", "password": "pw12345"})
    assert r.status_code == 200
    assert r.json()["access_token"]


async def test_login_wrong_password(client, register):
    await register("alice")
    r = await client.post("/auth/login", json={"username": "alice", "password": "wrong"})
    assert r.status_code == 401


async def test_login_unknown_user(client):
    r = await client.post("/auth/login", json={"username": "ghost", "password": "pw12345"})
    assert r.status_code == 401


async def test_refresh_token_flow(client, register):
    tokens = await register("alice")
    r = await client.post("/auth/refresh", json={"refresh_token": tokens["refresh"]})
    assert r.status_code == 200
    assert r.json()["access_token"]


async def test_refresh_rejects_access_token(client, register):
    tokens = await register("alice")
    r = await client.post("/auth/refresh", json={"refresh_token": tokens["token"]})
    assert r.status_code == 401


async def test_refresh_rejects_garbage(client):
    r = await client.post("/auth/refresh", json={"refresh_token": "not-a-jwt"})
    assert r.status_code == 401


async def test_users_me_requires_auth(client):
    r = await client.get("/users/me")
    assert r.status_code in (401, 403)


async def test_access_token_with_non_numeric_subject_is_rejected(client):
    token = jwt.encode(
        {"sub": "not-a-user-id", "type": "access"},
        settings.secret_key,
        algorithm=ALGORITHM,
    )
    r = await client.get(
        "/users/me", headers={"Authorization": f"Bearer {token}"}
    )
    assert r.status_code == 401


async def test_users_me_returns_profile(client, auth_headers):
    h = await auth_headers("alice")
    r = await client.get("/users/me", headers=h)
    assert r.status_code == 200
    body = r.json()
    assert body["username"] == "alice"
    assert body["email"] == "alice@example.com"
    assert body["display_name"] is None


async def test_users_me_patch_display_name(client, auth_headers):
    h = await auth_headers("alice")
    r = await client.patch("/users/me", headers=h, json={"display_name": "Alice W."})
    assert r.status_code == 200
    assert r.json()["display_name"] == "Alice W."

    r = await client.patch("/users/me", headers=h, json={"display_name": None})
    assert r.status_code == 200
    assert r.json()["display_name"] is None


async def test_expired_access_token_is_rejected(client, register):
    from datetime import datetime, timedelta, timezone

    await register("alice")
    expired = jwt.encode(
        {
            "sub": "1",
            "exp": datetime.now(timezone.utc) - timedelta(minutes=1),
            "type": "access",
        },
        settings.secret_key,
        algorithm=ALGORITHM,
    )
    r = await client.get("/users/me", headers={"Authorization": f"Bearer {expired}"})
    assert r.status_code == 401


async def test_refresh_token_is_not_an_access_token(client, register):
    tokens = await register("alice")
    r = await client.get(
        "/users/me", headers={"Authorization": f"Bearer {tokens['refresh']}"}
    )
    assert r.status_code == 401


async def test_non_bearer_authorization_scheme_is_rejected(client, register):
    tokens = await register("alice")
    for header in (
        f"Basic {tokens['token']}",
        tokens["token"],
        "Bearer",
    ):
        r = await client.get("/users/me", headers={"Authorization": header})
        assert r.status_code in (401, 403), header


async def test_refresh_flow_reuses_old_refresh_token(client, register):
    tokens = await register("alice")
    first = await client.post("/auth/refresh", json={"refresh_token": tokens["refresh"]})
    assert first.status_code == 200

    second = await client.post("/auth/refresh", json={"refresh_token": tokens["refresh"]})
    assert second.status_code == 200
    assert second.json()["access_token"]
