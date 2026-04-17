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
