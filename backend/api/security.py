from datetime import datetime, timedelta, timezone

import bcrypt
from jose import JWTError, jwt

from config import settings

ALGORITHM = "HS256"
BCRYPT_MAX_BYTES = 72


def _encode(password: str) -> bytes:
    encoded = password.encode("utf-8")
    if len(encoded) > BCRYPT_MAX_BYTES:
        raise ValueError("password exceeds bcrypt's 72-byte limit")
    return encoded


def hash_password(password: str) -> str:
    return bcrypt.hashpw(_encode(password), bcrypt.gensalt()).decode("utf-8")


def verify_password(plain: str, hashed: str) -> bool:
    try:
        return bcrypt.checkpw(_encode(plain), hashed.encode("utf-8"))
    except ValueError:
        return False


def create_access_token(user_id: int) -> str:
    expire = datetime.now(timezone.utc) + timedelta(minutes=settings.access_token_expire_minutes)
    return jwt.encode({"sub": str(user_id), "exp": expire, "type": "access"}, settings.secret_key, algorithm=ALGORITHM)


def create_refresh_token(user_id: int) -> str:
    expire = datetime.now(timezone.utc) + timedelta(days=settings.refresh_token_expire_days)
    return jwt.encode({"sub": str(user_id), "exp": expire, "type": "refresh"}, settings.secret_key, algorithm=ALGORITHM)


def decode_token(token: str) -> dict:
    try:
        return jwt.decode(token, settings.secret_key, algorithms=[ALGORITHM])
    except JWTError:
        return {}


def token_user_id(payload: dict) -> int | None:
    subject = payload.get("sub")
    if not isinstance(subject, str) or not subject.isascii() or not subject.isdecimal():
        return None
    user_id = int(subject)
    return user_id if user_id > 0 else None
