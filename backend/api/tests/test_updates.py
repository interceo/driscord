import pytest
import pytest_asyncio
from sqlalchemy import text


@pytest_asyncio.fixture
async def releases_dir(tmp_path, monkeypatch):
    import routers.updates as updates

    monkeypatch.setattr(updates, "RELEASES_DIR", tmp_path / "releases")
    monkeypatch.setattr(updates, "VERSIONS_FILE", tmp_path / "versions.json")
    return tmp_path / "releases"


@pytest_asyncio.fixture
async def admin_headers(engine, auth_headers):
    async def _headers(username: str) -> dict:
        headers = await auth_headers(username)
        async with engine.begin() as conn:
            await conn.execute(
                text("UPDATE users SET is_admin = 1 WHERE username = :u"),
                {"u": username},
            )
        return headers

    return _headers


async def test_upload_requires_admin(client, auth_headers, releases_dir):
    headers = await auth_headers("alice")
    r = await client.post(
        "/updates/upload",
        headers=headers,
        data={"version": "1.0.0", "platform": "linux"},
        files={"file": ("driscord", b"payload")},
    )
    assert r.status_code == 403
    assert not releases_dir.exists()


async def test_upload_and_download_roundtrip(client, admin_headers, releases_dir):
    headers = await admin_headers("root")
    uploaded = await client.post(
        "/updates/upload",
        headers=headers,
        data={"version": "1.0.0", "platform": "linux"},
        files={"file": ("driscord", b"payload")},
    )
    assert uploaded.status_code == 200, uploaded.text
    assert uploaded.json()["file"] == "driscord"
    assert (releases_dir / "linux" / "1.0.0" / "driscord").read_bytes() == b"payload"

    downloaded = await client.get("/updates/download/linux/1.0.0/driscord")
    assert downloaded.status_code == 200
    assert downloaded.content == b"payload"


async def test_upload_filename_cannot_escape_the_release_directory(
    client, admin_headers, releases_dir, tmp_path
):
    headers = await admin_headers("root")
    r = await client.post(
        "/updates/upload",
        headers=headers,
        data={"version": "1.0.0", "platform": "linux"},
        files={"file": ("../../../pwned", b"payload")},
    )
    assert r.status_code == 200, r.text
    assert r.json()["file"] == "pwned"
    assert (releases_dir / "linux" / "1.0.0" / "pwned").is_file()
    assert not (tmp_path / "pwned").exists()


async def _raw_get(path: str) -> tuple[int, bytes]:
    """Send an un-normalised path straight into the ASGI app.

    HTTP clients collapse `..` segments before the request leaves the process,
    so a traversal test that goes through httpx proves nothing. A raw socket
    (or any client that does not normalise) delivers the segments verbatim,
    which is exactly what the route sees in production.
    """
    from main import app

    scope = {
        "type": "http",
        "asgi": {"version": "3.0"},
        "http_version": "1.1",
        "method": "GET",
        "scheme": "http",
        "path": path,
        "raw_path": path.encode(),
        "query_string": b"",
        "headers": [(b"host", b"test")],
        "client": ("127.0.0.1", 1234),
        "server": ("test", 80),
        "root_path": "",
    }
    status = 0
    body = b""

    async def receive():
        return {"type": "http.request", "body": b"", "more_body": False}

    async def send(message):
        nonlocal status, body
        if message["type"] == "http.response.start":
            status = message["status"]
        elif message["type"] == "http.response.body":
            body += message.get("body", b"")

    await app(scope, receive, send)
    return status, body


@pytest.mark.parametrize(
    "path",
    [
        # `linux/..` lands back on RELEASES_DIR, which holds every platform's
        # releases and, one level up, the whole data directory.
        "/updates/download/linux/../secret.txt",
        "/updates/download/../linux/secret.txt",
        "/updates/download/linux/1.0.0/..",
        "/updates/download/linux/./secret.txt",
    ],
)
async def test_download_rejects_traversal(releases_dir, tmp_path, path):
    # The directories must exist for the OS to resolve the `..` at all — that
    # is the state a server reaches after the first upload.
    (releases_dir / "linux" / "1.0.0").mkdir(parents=True)
    (releases_dir / "secret.txt").write_text("SECRET_KEY=hunter2")
    (tmp_path / "secret.txt").write_text("SECRET_KEY=hunter2")

    status, body = await _raw_get(path)
    assert status in (400, 404), (status, body)
    assert b"hunter2" not in body


async def test_download_rejects_unknown_platform(client, releases_dir):
    r = await client.get("/updates/download/bsd/1.0.0/driscord")
    assert r.status_code == 400
