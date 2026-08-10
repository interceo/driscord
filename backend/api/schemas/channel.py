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


class ChannelAccessResponse(BaseModel):
    """Answer to "may this token join this channel?", used by the SFU."""

    channel_id: int
    server_id: int
    user_id: int
    username: str
    display_name: str | None = None


class ChannelResponse(BaseModel):
    id: int
    server_id: int
    name: str
    kind: ChannelKind
    position: int
    created_at: datetime

    model_config = {"from_attributes": True}
