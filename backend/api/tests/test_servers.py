async def _create_server(client, headers, name="My Server", description=None):
    payload = {"name": name}
    if description is not None:
        payload["description"] = description
    r = await client.post("/servers/", headers=headers, json=payload)
    assert r.status_code == 201, r.text
    return r.json()


async def _invite(client, owner_headers, member_headers, server_id):
    created = await client.post(
        f"/servers/{server_id}/invites/", headers=owner_headers
    )
    assert created.status_code == 201, created.text
    accepted = await client.post(
        f"/invites/{created.json()['code']}", headers=member_headers
    )
    assert accepted.status_code == 200, accepted.text


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


async def test_non_member_cannot_read_server_or_member_list(client, auth_headers):
    owner = await auth_headers("alice")
    outsider = await auth_headers("bob")
    server = await _create_server(client, owner)

    details = await client.get(f"/servers/{server['id']}", headers=outsider)
    members = await client.get(
        f"/servers/{server['id']}/members", headers=outsider
    )
    assert details.status_code == 403
    assert members.status_code == 403


async def test_patch_server_owner_only(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)

    r = await client.patch(f"/servers/{s['id']}", headers=ha, json={"name": "Renamed"})
    assert r.status_code == 200
    assert r.json()["name"] == "Renamed"

    r = await client.patch(f"/servers/{s['id']}", headers=hb, json={"name": "Hack"})
    assert r.status_code == 403


async def test_patch_server_can_clear_description(client, auth_headers):
    headers = await auth_headers("alice")
    server = await _create_server(client, headers, description="temporary")

    r = await client.patch(
        f"/servers/{server['id']}", headers=headers, json={"description": None}
    )
    assert r.status_code == 200
    assert r.json()["description"] is None


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


async def test_direct_join_requires_invite(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)

    r = await client.post(f"/servers/{s['id']}/members", headers=hb)
    assert r.status_code == 403


async def test_accept_invite_twice_is_idempotent(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)

    invite = await client.post(f"/servers/{s['id']}/invites/", headers=ha)
    code = invite.json()["code"]
    first = await client.post(f"/invites/{code}", headers=hb)
    second = await client.post(f"/invites/{code}", headers=hb)
    assert first.json()["status"] == "joined"
    assert second.json()["status"] == "already_member"


async def test_leave_server(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)

    await _invite(client, ha, hb, s["id"])
    r = await client.delete(f"/servers/{s['id']}/members", headers=hb)
    assert r.status_code == 204

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
    await _invite(client, ha, hb, s["id"])

    r = await client.get(f"/servers/{s['id']}/members", headers=ha)
    assert r.status_code == 200
    usernames = sorted(m["username"] for m in r.json())
    assert usernames == ["alice", "bob"]


async def test_owner_can_add_user_from_directory(client, auth_headers):
    ha = await auth_headers("alice")
    await auth_headers("bob")
    s = await _create_server(client, ha)

    r = await client.post(f"/servers/{s['id']}/members/2", headers=ha)
    assert r.status_code == 201
    assert r.json() == {"status": "added", "user_id": 2}

    members = await client.get(f"/servers/{s['id']}/members", headers=ha)
    assert sorted(m["username"] for m in members.json()) == ["alice", "bob"]


async def test_only_owner_can_add_user(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    hc = await auth_headers("carol")
    s = await _create_server(client, ha)

    r = await client.post(f"/servers/{s['id']}/members/3", headers=hb)
    assert r.status_code == 403

    r = await client.post(f"/servers/{s['id']}/members/999", headers=ha)
    assert r.status_code == 404

    await client.post(f"/servers/{s['id']}/members/3", headers=ha)
    duplicate = await client.post(f"/servers/{s['id']}/members/3", headers=ha)
    assert duplicate.status_code == 409


async def test_server_endpoints_require_auth(client):
    r = await client.get("/servers/")
    assert r.status_code in (401, 403)
    r = await client.post("/servers/", json={"name": "x"})
    assert r.status_code in (401, 403)
