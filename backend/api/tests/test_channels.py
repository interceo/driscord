async def _create_server(client, headers, name="S"):
    r = await client.post("/servers/", headers=headers, json={"name": name})
    assert r.status_code == 201
    return r.json()


async def _join(client, headers, server_id):
    r = await client.post(f"/servers/{server_id}/members", headers=headers)
    assert r.status_code == 201


async def test_owner_creates_channel(client, auth_headers):
    h = await auth_headers("alice")
    s = await _create_server(client, h)
    r = await client.post(
        f"/servers/{s['id']}/channels/",
        headers=h,
        json={"name": "general", "kind": "voice"},
    )
    assert r.status_code == 201
    body = r.json()
    assert body["name"] == "general"
    assert body["kind"] == "voice"
    assert body["server_id"] == s["id"]


async def test_default_kind_is_voice(client, auth_headers):
    h = await auth_headers("alice")
    s = await _create_server(client, h)
    r = await client.post(f"/servers/{s['id']}/channels/", headers=h, json={"name": "g"})
    assert r.status_code == 201
    assert r.json()["kind"] == "voice"


async def test_non_member_cannot_list_channels(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)
    r = await client.get(f"/servers/{s['id']}/channels/", headers=hb)
    assert r.status_code == 403


async def test_member_can_list_channels(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)
    await client.post(f"/servers/{s['id']}/channels/", headers=ha, json={"name": "g"})
    await _join(client, hb, s["id"])
    r = await client.get(f"/servers/{s['id']}/channels/", headers=hb)
    assert r.status_code == 200
    assert [c["name"] for c in r.json()] == ["g"]


async def test_channels_sorted_by_position(client, auth_headers):
    h = await auth_headers("alice")
    s = await _create_server(client, h)
    for name, pos in [("c", 2), ("a", 0), ("b", 1)]:
        await client.post(
            f"/servers/{s['id']}/channels/",
            headers=h,
            json={"name": name, "position": pos},
        )
    r = await client.get(f"/servers/{s['id']}/channels/", headers=h)
    assert [c["name"] for c in r.json()] == ["a", "b", "c"]


async def test_member_cannot_create_channel(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)
    await _join(client, hb, s["id"])

    r = await client.post(
        f"/servers/{s['id']}/channels/", headers=hb, json={"name": "x", "kind": "voice"}
    )
    assert r.status_code == 403


async def test_get_channel(client, auth_headers):
    h = await auth_headers("alice")
    s = await _create_server(client, h)
    c = (await client.post(
        f"/servers/{s['id']}/channels/", headers=h, json={"name": "g"}
    )).json()
    r = await client.get(f"/servers/{s['id']}/channels/{c['id']}", headers=h)
    assert r.status_code == 200
    assert r.json()["id"] == c["id"]


async def test_get_unknown_channel_404(client, auth_headers):
    h = await auth_headers("alice")
    s = await _create_server(client, h)
    r = await client.get(f"/servers/{s['id']}/channels/999", headers=h)
    assert r.status_code == 404


async def test_patch_channel_owner_only(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)
    c = (await client.post(
        f"/servers/{s['id']}/channels/", headers=ha, json={"name": "g"}
    )).json()
    await _join(client, hb, s["id"])

    r = await client.patch(
        f"/servers/{s['id']}/channels/{c['id']}", headers=hb, json={"name": "hack"}
    )
    assert r.status_code == 403

    r = await client.patch(
        f"/servers/{s['id']}/channels/{c['id']}", headers=ha, json={"name": "renamed"}
    )
    assert r.status_code == 200
    assert r.json()["name"] == "renamed"


async def test_delete_channel_owner_only(client, auth_headers):
    ha = await auth_headers("alice")
    hb = await auth_headers("bob")
    s = await _create_server(client, ha)
    c = (await client.post(
        f"/servers/{s['id']}/channels/", headers=ha, json={"name": "g"}
    )).json()
    await _join(client, hb, s["id"])

    r = await client.delete(f"/servers/{s['id']}/channels/{c['id']}", headers=hb)
    assert r.status_code == 403

    r = await client.delete(f"/servers/{s['id']}/channels/{c['id']}", headers=ha)
    assert r.status_code == 204

    r = await client.get(f"/servers/{s['id']}/channels/{c['id']}", headers=ha)
    assert r.status_code == 404


async def test_delete_server_cascades_channels(client, auth_headers):
    h = await auth_headers("alice")
    s = await _create_server(client, h)
    c = (await client.post(
        f"/servers/{s['id']}/channels/", headers=h, json={"name": "g"}
    )).json()

    r = await client.delete(f"/servers/{s['id']}", headers=h)
    assert r.status_code == 204

    # Channel should be gone too (server 404 wraps it)
    r = await client.get(f"/servers/{s['id']}/channels/{c['id']}", headers=h)
    assert r.status_code == 404


async def test_unknown_server_returns_404(client, auth_headers):
    h = await auth_headers("alice")
    r = await client.get("/servers/999/channels/", headers=h)
    assert r.status_code == 404
