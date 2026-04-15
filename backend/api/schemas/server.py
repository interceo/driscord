from datetime import datetime

from pydantic import BaseModel


class ServerCreate(BaseModel):
    name: str
    description: str | None = None


class ServerUpdate(BaseModel):
    name: str | None = None
    description: str | None = None


class ServerResponse(BaseModel):
    id: int
    name: str
    description: str | None
    owner_id: int
    created_at: datetime

    model_config = {"from_attributes": True}


class ServerMemberResponse(BaseModel):
    user_id: int
    username: str
    joined_at: datetime
