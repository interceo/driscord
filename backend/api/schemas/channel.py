from datetime import datetime

from pydantic import BaseModel


class ChannelCreate(BaseModel):
    name: str
    description: str | None = None


class ChannelUpdate(BaseModel):
    name: str | None = None
    description: str | None = None


class ChannelMemberResponse(BaseModel):
    user_id: int
    username: str
    joined_at: datetime


class ChannelResponse(BaseModel):
    id: int
    name: str
    description: str | None
    owner_id: int
    created_at: datetime

    model_config = {"from_attributes": True}
