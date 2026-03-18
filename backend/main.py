"""
Driscord update backend.

Endpoints:
  GET  /check_updates?version=X.Y.Z&platform=linux|windows
  POST /update   (multipart: version, platform, notes, file)

Auth for POST: set ADMIN_TOKEN env var, pass as X-Admin-Token header.
Run: uvicorn main:app --host 0.0.0.0 --port 8000
"""
from __future__ import annotations

import json
import os
import shutil
from pathlib import Path
from typing import Annotated, Optional

from fastapi import FastAPI, File, Form, Header, HTTPException, UploadFile
from fastapi.responses import FileResponse

# ---------------------------------------------------------------------------
# Storage layout
# ---------------------------------------------------------------------------
DATA_DIR = Path(os.getenv("DRISCORD_DATA_DIR", Path(__file__).parent / "data"))
VERSIONS_FILE = DATA_DIR / "versions.json"
RELEASES_DIR = DATA_DIR / "releases"

ADMIN_TOKEN = os.getenv("ADMIN_TOKEN", "")

app = FastAPI(title="Driscord Update Server", version="1.0.0")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _load_versions() -> dict:
    if VERSIONS_FILE.exists():
        return json.loads(VERSIONS_FILE.read_text())
    return {}


def _save_versions(data: dict) -> None:
    VERSIONS_FILE.parent.mkdir(parents=True, exist_ok=True)
    VERSIONS_FILE.write_text(json.dumps(data, indent=2))


def _parse_version(v: str) -> tuple[int, ...]:
    """Parse 'X.Y.Z' into a comparable tuple."""
    try:
        return tuple(int(x) for x in v.strip().split("."))
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid version format: {v!r}")


def _check_auth(token: Optional[str]) -> None:
    if ADMIN_TOKEN and token != ADMIN_TOKEN:
        raise HTTPException(status_code=403, detail="Forbidden: invalid or missing X-Admin-Token")


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------

@app.get("/check_updates")
def check_updates(version: str, platform: str = "linux") -> dict:
    """
    Returns whether a newer build is available.

    Response:
      { "update_available": bool, "latest_version": str, "notes": str }
    """
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


@app.post("/update")
async def post_update(
    version: Annotated[str, Form()],
    platform: Annotated[str, Form()],
    notes: Annotated[str, Form()] = "",
    file: Annotated[Optional[UploadFile], File()] = None,
    x_admin_token: Annotated[Optional[str], Header()] = None,
) -> dict:
    """
    Upload a new release artifact.

    Fields (multipart/form-data):
      version   — version string, e.g. "0.4.0"
      platform  — "linux" | "windows"
      notes     — optional release notes
      file      — release archive (optional; can register a version without a file)

    Header:
      X-Admin-Token — required when ADMIN_TOKEN env var is set
    """
    _check_auth(x_admin_token)

    if platform not in ("linux", "windows"):
        raise HTTPException(status_code=400, detail="platform must be 'linux' or 'windows'")

    _parse_version(version)  # validate format

    filename: Optional[str] = None
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


@app.get("/download/{platform}/{version}/{filename}")
def download_release(platform: str, version: str, filename: str) -> FileResponse:
    """Download a specific release artifact."""
    path = RELEASES_DIR / platform / version / filename
    if not path.exists():
        raise HTTPException(status_code=404, detail="File not found")
    return FileResponse(path, filename=filename)


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}
