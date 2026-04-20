from datetime import datetime

from pydantic import BaseModel


class UserResponse(BaseModel):
    id: int
    username: str
    email: str
    display_name: str | None
    avatar_url: str | None
    created_at: datetime

    model_config = {"from_attributes": True}


class UserUpdateRequest(BaseModel):
    display_name: str | None = None
