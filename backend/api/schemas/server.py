from datetime import datetime

from pydantic import BaseModel

from schemas.common import ServerDescription, ServerName


class ServerCreate(BaseModel):
    name: ServerName
    description: ServerDescription | None = None


class ServerUpdate(BaseModel):
    name: ServerName | None = None
    description: ServerDescription | None = None


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


class ServerInviteResponse(BaseModel):
    code: str
    server_id: int
    creator_id: int
    created_at: datetime

    model_config = {"from_attributes": True}


class InviteAcceptResponse(BaseModel):
    server_id: int
    status: str
