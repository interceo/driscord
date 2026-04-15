from contextlib import asynccontextmanager

from fastapi import FastAPI

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
