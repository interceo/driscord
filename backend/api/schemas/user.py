from pydantic import BaseModel

from schemas.common import AvatarUrl, DisplayName


class UserResponse(BaseModel):
    id: int
    username: str
    display_name: str | None
    avatar_url: AvatarUrl

    model_config = {"from_attributes": True}


# Includes private fields (email). Returned only from /users/me to the
# authenticated owner — never expose this from public lookups.
class MeResponse(BaseModel):
    id: int
    username: str
    display_name: str | None
    avatar_url: AvatarUrl
    email: str

    model_config = {"from_attributes": True}


class UserUpdateRequest(BaseModel):
    display_name: DisplayName | None = None
