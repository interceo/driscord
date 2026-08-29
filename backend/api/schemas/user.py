from pydantic import BaseModel

from schemas.common import AvatarUrl, DisplayName


class UserResponse(BaseModel):
    id: int
    username: str
    display_name: str | None
    avatar_url: AvatarUrl

    model_config = {"from_attributes": True}


class MeResponse(BaseModel):
    id: int
    username: str
    display_name: str | None
    avatar_url: AvatarUrl
    email: str

    model_config = {"from_attributes": True}


class UserUpdateRequest(BaseModel):
    display_name: DisplayName | None = None
