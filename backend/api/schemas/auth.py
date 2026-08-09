from pydantic import BaseModel, EmailStr

from schemas.common import AvatarUrl


class RegisterRequest(BaseModel):
    username: str
    email: EmailStr
    password: str


class LoginRequest(BaseModel):
    username: str
    password: str


class TokenResponse(BaseModel):
    access_token: str
    refresh_token: str
    user_id: int
    token_type: str = "bearer"
    avatar_url: AvatarUrl = None
    display_name: str | None = None


class RefreshRequest(BaseModel):
    refresh_token: str
