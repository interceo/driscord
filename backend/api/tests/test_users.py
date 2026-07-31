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
