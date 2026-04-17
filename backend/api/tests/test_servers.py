async def _create_server(client, headers, name="My Server", description=None):
    payload = {"name": name}
    if description is not None:
        payload["description"] = description
    r = await client.post("/servers/", headers=headers, json=payload)
    assert r.status_code == 201, r.text
    return r.json()


async def test_create_server(client, auth_headers):
    h = await auth_headers("alice")
    r = await client.post(
        "/servers/", headers=h, json={"name": "Team", "description": "hi"}
    )
    assert r.status_code == 201
    body = r.json()
    assert body["name"] == "Team"
    assert body["description"] == "hi"
    assert body["owner_id"] == 1


async def test_list_servers_returns_only_membered(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    await _create_server(client, ha, name="Alice's Place")

    r = await client.get("/servers/", headers=ha)
    assert r.status_code == 200
    assert [s["name"] for s in r.json()] == ["Alice's Place"]

    r = await client.get("/servers/", headers=hb)
    assert r.status_code == 200
    assert r.json() == []


async def test_get_server(client, auth_headers):
    h = await auth_headers("alice")
    s = await _create_server(client, h)
    r = await client.get(f"/servers/{s['id']}", headers=h)
    assert r.status_code == 200
    assert r.json()["id"] == s["id"]


async def test_get_unknown_server_404(client, auth_headers):
    h = await auth_headers("alice")
    r = await client.get("/servers/999", headers=h)
    assert r.status_code == 404


async def test_patch_server_owner_only(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)

    r = await client.patch(f"/servers/{s['id']}", headers=ha, json={"name": "Renamed"})
    assert r.status_code == 200
    assert r.json()["name"] == "Renamed"

    # bob is not even a member, still 403 (ownership check happens regardless of membership)
    r = await client.patch(f"/servers/{s['id']}", headers=hb, json={"name": "Hack"})
    assert r.status_code == 403


async def test_delete_server_owner_only(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)

    r = await client.delete(f"/servers/{s['id']}", headers=hb)
    assert r.status_code == 403

    r = await client.delete(f"/servers/{s['id']}", headers=ha)
    assert r.status_code == 204

    r = await client.get(f"/servers/{s['id']}", headers=ha)
    assert r.status_code == 404


async def test_join_server(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)

    r = await client.post(f"/servers/{s['id']}/members", headers=hb)
    assert r.status_code == 201
    assert r.json() == {"status": "joined"}


async def test_join_duplicate_conflict(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)

    await client.post(f"/servers/{s['id']}/members", headers=hb)
    r = await client.post(f"/servers/{s['id']}/members", headers=hb)
    assert r.status_code == 409


async def test_leave_server(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)

    await client.post(f"/servers/{s['id']}/members", headers=hb)
    r = await client.delete(f"/servers/{s['id']}/members", headers=hb)
    assert r.status_code == 204

    # Leave again → 404 (no longer a member)
    r = await client.delete(f"/servers/{s['id']}/members", headers=hb)
    assert r.status_code == 404


async def test_owner_cannot_leave(client, auth_headers):
    h = await auth_headers("alice")
    s = await _create_server(client, h)
    r = await client.delete(f"/servers/{s['id']}/members", headers=h)
    assert r.status_code == 400


async def test_list_members(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)
    await client.post(f"/servers/{s['id']}/members", headers=hb)

    r = await client.get(f"/servers/{s['id']}/members", headers=ha)
    assert r.status_code == 200
    usernames = sorted(m["username"] for m in r.json())
    assert usernames == ["alice", "bob"]


async def test_server_endpoints_require_auth(client):
    r = await client.get("/servers/")
    assert r.status_code in (401, 403)
    r = await client.post("/servers/", json={"name": "x"})
    assert r.status_code in (401, 403)
