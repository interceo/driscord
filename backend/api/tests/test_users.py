async def test_list_users_requires_auth_and_hides_private_fields(client, auth_headers):
    ha = await auth_headers("alice")
    await auth_headers("bob")

    unauthorized = await client.get("/users/")
    assert unauthorized.status_code in (401, 403)

    r = await client.get("/users/", headers=ha)
    assert r.status_code == 200
    assert [u["username"] for u in r.json()] == ["alice", "bob"]
    assert all("email" not in u for u in r.json())
    assert all("hashed_password" not in u for u in r.json())


async def test_missing_avatar_file_is_not_advertised(
    client, auth_headers, tmp_path, monkeypatch
):
    from config import settings

    monkeypatch.setattr(settings, "data_dir", tmp_path)
    headers = await auth_headers("alice")

    uploaded = await client.put(
        "/users/1/avatar",
        headers=headers,
        files={"file": ("avatar.png", b"placeholder", "image/png")},
    )
    assert uploaded.status_code == 200
    stored_url = uploaded.json()["avatar_url"]
    assert stored_url == "avatars/1.png"
    (tmp_path / stored_url).unlink()

    me = await client.get("/users/me", headers=headers)
    assert me.status_code == 200
    assert me.json()["avatar_url"] is None

    lookup = await client.get("/users/lookup?username=alice")
    assert lookup.status_code == 200
    assert lookup.json()["avatar_url"] is None

    login = await client.post(
        "/auth/login", json={"username": "alice", "password": "pw12345"}
    )
    assert login.status_code == 200
    assert login.json()["avatar_url"] is None

    avatar = await client.get("/users/1/avatar")
    assert avatar.status_code == 404


async def test_empty_avatar_is_rejected(client, auth_headers):
    headers = await auth_headers("alice")
    uploaded = await client.put(
        "/users/1/avatar",
        headers=headers,
        files={"file": ("avatar.png", b"", "image/png")},
    )
    assert uploaded.status_code == 400
