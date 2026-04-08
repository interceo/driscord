"""Update distribution endpoints — migrated from the old backend/main.py."""
from __future__ import annotations

import json
import shutil
from pathlib import Path
from typing import Annotated

from fastapi import APIRouter, Depends, File, Form, HTTPException, UploadFile
from fastapi.responses import FileResponse

from config import settings
from dependencies import get_current_user
from models.user import User

router = APIRouter(prefix="/updates", tags=["updates"])

VERSIONS_FILE = settings.data_dir / "versions.json"
RELEASES_DIR = settings.data_dir / "releases"


def _load_versions() -> dict:
    if VERSIONS_FILE.exists():
        return json.loads(VERSIONS_FILE.read_text())
    return {}


def _save_versions(data: dict) -> None:
    VERSIONS_FILE.parent.mkdir(parents=True, exist_ok=True)
    VERSIONS_FILE.write_text(json.dumps(data, indent=2))


def _parse_version(v: str) -> tuple[int, ...]:
    try:
        return tuple(int(x) for x in v.strip().split("."))
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid version format: {v!r}")


@router.get("/check")
def check_updates(version: str, platform: str = "linux") -> dict:
    versions = _load_versions()
    latest = versions.get(platform)

    if not latest:
        return {"update_available": False, "latest_version": version, "notes": ""}

    latest_ver = latest["version"]
    update_available = _parse_version(latest_ver) > _parse_version(version)
    return {
        "update_available": update_available,
        "latest_version": latest_ver,
        "notes": latest.get("notes", ""),
    }


@router.post("/upload")
async def upload_update(
    version: Annotated[str, Form()],
    platform: Annotated[str, Form()],
    notes: Annotated[str, Form()] = "",
    file: Annotated[UploadFile | None, File()] = None,
    _current_user: User = Depends(get_current_user),
) -> dict:
    if platform not in ("linux", "windows"):
        raise HTTPException(status_code=400, detail="platform must be 'linux' or 'windows'")

    _parse_version(version)

    filename: str | None = None
    if file is not None:
        release_path = RELEASES_DIR / platform / version
        release_path.mkdir(parents=True, exist_ok=True)
        dest = release_path / file.filename  # type: ignore[arg-type]
        with open(dest, "wb") as fp:
            shutil.copyfileobj(file.file, fp)
        filename = file.filename

    versions = _load_versions()
    versions[platform] = {"version": version, "notes": notes, "file": filename}
    _save_versions(versions)

    return {"status": "ok", "version": version, "platform": platform, "file": filename}


@router.get("/download/{platform}/{version}/{filename}")
def download_release(platform: str, version: str, filename: str) -> FileResponse:
    path: Path = RELEASES_DIR / platform / version / filename
    if not path.exists():
        raise HTTPException(status_code=404, detail="File not found")
    return FileResponse(path, filename=filename)
