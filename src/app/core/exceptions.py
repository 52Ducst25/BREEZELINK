"""Application exceptions + FastAPI handler registration.

All handlers return a consistent ``{"detail": ...}`` body. Internal errors are
masked (generic 500) so stack traces never leak to clients.
"""

import logging

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

logger = logging.getLogger("breezelink")


class AppException(Exception):
    """Base application exception carrying an HTTP status + message."""

    status_code: int = 400

    def __init__(self, detail: str, status_code: int | None = None) -> None:
        self.detail = detail
        if status_code is not None:
            self.status_code = status_code
        super().__init__(detail)


class NotFoundError(AppException):
    status_code = 404


class UnauthorizedError(AppException):
    status_code = 401


class ForbiddenError(AppException):
    status_code = 403


class ConflictError(AppException):
    status_code = 409


def register_exception_handlers(app: FastAPI) -> None:
    """Attach handlers converting exceptions into consistent JSON responses."""

    @app.exception_handler(AppException)
    async def _handle_app_exception(_: Request, exc: AppException) -> JSONResponse:
        return JSONResponse(status_code=exc.status_code, content={"detail": exc.detail})

    @app.exception_handler(Exception)
    async def _handle_unexpected(_: Request, exc: Exception) -> JSONResponse:
        # Never leak internals to the client.
        logger.exception("Unhandled error: %s", exc)
        return JSONResponse(
            status_code=500, content={"detail": "Internal server error"}
        )
