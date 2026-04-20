from pydantic import BaseModel


class UserResponse(BaseModel):
    id: int
    username: str
    display_name: str | None
    avatar_url: str | None

    model_config = {"from_attributes": True}


class UserUpdateRequest(BaseModel):
    display_name: str | None = None
