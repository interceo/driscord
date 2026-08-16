import pytest
from pydantic import ValidationError

from config import Settings


def test_production_secret_must_have_cryptographic_length():
    with pytest.raises(ValidationError, match="at least 32 UTF-8 bytes"):
        Settings(
            database_url="sqlite+aiosqlite:///:memory:",
            secret_key="too-short",
            allow_insecure_secret=False,
            _env_file=None,
        )


def test_throwaway_environment_can_explicitly_allow_short_secret():
    settings = Settings(
        database_url="sqlite+aiosqlite:///:memory:",
        secret_key="test-secret",
        allow_insecure_secret=True,
        _env_file=None,
    )
    assert settings.secret_key == "test-secret"
