from pathlib import Path

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=Path(__file__).parent / ".env",
        env_file_encoding="utf-8",
    )

    database_url: str = "postgresql+asyncpg://user:pass@localhost:5432/driscord"
    secret_key: str = "change-me"
    api_port: int = 8000
    data_dir: Path = Path(__file__).parent / "data"

    # JWT
    access_token_expire_minutes: int = 30
    refresh_token_expire_days: int = 7


settings = Settings()
