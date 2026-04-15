from datetime import datetime

from pydantic import BaseModel

from models.channel import ChannelKind


class ChannelCreate(BaseModel):
    name: str
    kind: ChannelKind = ChannelKind.voice
    position: int = 0


class ChannelUpdate(BaseModel):
    name: str | None = None
    position: int | None = None


class ChannelResponse(BaseModel):
    id: int
    server_id: int
    name: str
    kind: ChannelKind
    position: int
    created_at: datetime

    model_config = {"from_attributes": True}
