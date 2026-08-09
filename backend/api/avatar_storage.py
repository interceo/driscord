from pathlib import Path

from config import settings


def avatar_path(value: object) -> Path | None:
    """Resolve an existing avatar below DATA_DIR, rejecting stale/unsafe paths."""
    if not isinstance(value, str) or not value.strip():
        return None

    root = settings.data_dir.resolve()
    candidate = (root / value).resolve()
    try:
        candidate.relative_to(root)
    except ValueError:
        return None
    return candidate if candidate.is_file() else None


def available_avatar_url(value: object) -> str | None:
    return value if avatar_path(value) else None
