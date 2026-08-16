from typing import Annotated

from pydantic import BaseModel, EmailStr, StringConstraints

from schemas.common import AvatarUrl, NewPassword, Password, Token, Username

Email = Annotated[EmailStr, StringConstraints(max_length=255)]


class RegisterRequest(BaseModel):
    username: Username
    email: Email
    password: NewPassword


class LoginRequest(BaseModel):
    username: Username
    password: Password


class TokenResponse(BaseModel):
    access_token: str
    refresh_token: str
    user_id: int
    token_type: str = "bearer"
    avatar_url: AvatarUrl = None
    display_name: str | None = None


class RefreshRequest(BaseModel):
    refresh_token: Token
