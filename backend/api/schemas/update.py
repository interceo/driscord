from pydantic import BaseModel


class UpdateCheckResponse(BaseModel):
    update_available: bool
    latest_version: str
    notes: str


class UpdateUploadResponse(BaseModel):
    status: str
    version: str
    platform: str
    file: str | None
