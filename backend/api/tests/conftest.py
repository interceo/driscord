"""Pytest fixtures: in-memory SQLite engine per test + httpx AsyncClient with get_db override.

Setting DATABASE_URL before any app imports ensures the app-level global engine
(in database.py) does not try to connect to Postgres during test collection.
"""
import os

os.environ.setdefault("DATABASE_URL", "sqlite+aiosqlite:///:memory:")
os.environ.setdefault("SECRET_KEY", "test-secret")

import pytest_asyncio  # noqa: E402
from httpx import ASGITransport, AsyncClient  # noqa: E402
from sqlalchemy import event  # noqa: E402
from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine  # noqa: E402
from sqlalchemy.pool import StaticPool  # noqa: E402

from database import Base  # noqa: E402
from dependencies import get_db  # noqa: E402
from main import app  # noqa: E402


@pytest_asyncio.fixture
async def engine():
    eng = create_async_engine(
        "sqlite+aiosqlite:///:memory:",
        connect_args={"check_same_thread": False},
        poolclass=StaticPool,
    )

    @event.listens_for(eng.sync_engine, "connect")
    def _fk_on(dbapi_conn, _):
        cur = dbapi_conn.cursor()
        cur.execute("PRAGMA foreign_keys=ON")
        cur.close()

    async with eng.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    yield eng
    await eng.dispose()


@pytest_asyncio.fixture
async def client(engine):
    session_factory = async_sessionmaker(engine, expire_on_commit=False)

    async def _get_db():
        async with session_factory() as s:
            yield s

    app.dependency_overrides[get_db] = _get_db
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as ac:
        yield ac
    app.dependency_overrides.clear()


@pytest_asyncio.fixture
async def register(client):
    async def _register(username: str, password: str = "pw12345") -> dict:
        r = await client.post(
            "/auth/register",
            json={
                "username": username,
                "email": f"{username}@example.com",
                "password": password,
            },
        )
        assert r.status_code == 201, r.text
        data = r.json()
        return {"token": data["access_token"], "refresh": data["refresh_token"]}

    return _register


@pytest_asyncio.fixture
async def auth_headers(register):
    async def _headers(username: str) -> dict:
        tokens = await register(username)
        return {"Authorization": f"Bearer {tokens['token']}"}

    return _headers
