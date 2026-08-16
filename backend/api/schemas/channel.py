from datetime import datetime

from pydantic import BaseModel, Field

from models.channel import ChannelKind
from schemas.common import ChannelName


class ChannelCreate(BaseModel):
    name: ChannelName
    kind: ChannelKind = ChannelKind.voice
    position: int = Field(default=0, ge=0, le=1_000_000)


class ChannelUpdate(BaseModel):
    name: ChannelName | None = None
    position: int | None = Field(default=None, ge=0, le=1_000_000)


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
