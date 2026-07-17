"""Shared response DTOs reused across domains."""

from pydantic import BaseModel


class MessageResponse(BaseModel):
    """Generic ``{"detail": ...}`` response body."""

    detail: str


class HealthResponse(BaseModel):
    """Liveness probe response body."""

    status: str = "ok"
