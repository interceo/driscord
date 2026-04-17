import logging
from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict

logger = logging.getLogger("driscord.config")

ENV_FILE = Path(__file__).parent / ".env"


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=ENV_FILE,
        env_file_encoding="utf-8",
    )

    database_url: str = "postgresql+asyncpg://user:pass@localhost:5432/driscord"
    secret_key: str = "change-me"
    api_port: int = 8000
    data_dir: Path = Path(__file__).parent / "data"

    # JWT
    access_token_expire_minutes: int = 30
    refresh_token_expire_days: int = 7


def _redact(url: str) -> str:
    # postgresql+asyncpg://user:pass@host:port/db -> postgresql+asyncpg://user:***@host:port/db
    if "://" not in url or "@" not in url:
        return url
    scheme, rest = url.split("://", 1)
    creds, tail = rest.split("@", 1)
    if ":" in creds:
        user, _ = creds.split(":", 1)
        creds = f"{user}:***"
    return f"{scheme}://{creds}@{tail}"


def _log_settings(s: "Settings") -> None:
    env_exists = ENV_FILE.exists()
    env_readable = env_exists and ENV_FILE.is_file()
    try:
        env_readable = env_readable and bool(ENV_FILE.read_text())
    except OSError:
        env_readable = False

    logger.info(".env path: %s (exists=%s, readable=%s)", ENV_FILE, env_exists, env_readable)
    logger.info("database_url: %s", _redact(s.database_url))
    logger.info("api_port: %s", s.api_port)
    logger.info("secret_key: %s", "***set***" if s.secret_key != "change-me" else "DEFAULT (change-me)")
    logger.info("data_dir: %s", s.data_dir)
    logger.info("jwt access/refresh: %sm / %sd", s.access_token_expire_minutes, s.refresh_token_expire_days)


settings = Settings()
_log_settings(settings)
