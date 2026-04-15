import logging
from contextlib import asynccontextmanager

import uvicorn
from fastapi import FastAPI

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)

from config import settings  # noqa: E402 — must be after logging.basicConfig
from database import Base, engine
from routers import auth, channels, health, servers, updates, users


@asynccontextmanager
async def lifespan(app: FastAPI):
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    yield


app = FastAPI(title="Driscord API", version="1.0.0", lifespan=lifespan)

app.include_router(health.router)
app.include_router(auth.router)
app.include_router(users.router)
app.include_router(servers.router)
app.include_router(channels.router)
app.include_router(updates.router)


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=settings.api_port)
