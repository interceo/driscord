from pathlib import Path

from config import settings
from storage_paths import contained_path


def avatar_path(value: object) -> Path | None:
    """Resolve an existing avatar below DATA_DIR, rejecting stale/unsafe paths."""
    if not isinstance(value, str) or not value.strip():
        return None

    candidate = contained_path(settings.data_dir, value)
    return candidate if candidate and candidate.is_file() else None


def available_avatar_url(value: object) -> str | None:
    return value if avatar_path(value) else None
