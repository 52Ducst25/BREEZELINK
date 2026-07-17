"""HTTP middleware: request logging + JWT -> contextvars extraction.

The middleware sets org/user contextvars when a valid Bearer token is present.
It stays SILENT on missing/invalid tokens — actual 401 enforcement happens in
the auth dependencies (api/deps.py), so public routes remain reachable.
"""

import logging
import time

from starlette.middleware.base import BaseHTTPMiddleware
from starlette.requests import Request
from starlette.types import ASGIApp

from app.core.security import decode_token
from app.core.tenant import set_current_org, set_current_user

logger = logging.getLogger("aircon.request")


class RequestContextMiddleware(BaseHTTPMiddleware):
    """Log each request and populate tenant contextvars from the Bearer token."""

    def __init__(self, app: ASGIApp) -> None:
        super().__init__(app)

    async def dispatch(self, request: Request, call_next):
        # Reset context for this request (contextvars are task-local).
        set_current_org(None)
        set_current_user(None)
        self._populate_context(request)

        start = time.perf_counter()
        response = await call_next(request)
        elapsed_ms = (time.perf_counter() - start) * 1000
        logger.info(
            "%s %s -> %s (%.1fms)",
            request.method,
            request.url.path,
            response.status_code,
            elapsed_ms,
        )
        return response

    @staticmethod
    def _populate_context(request: Request) -> None:
        auth = request.headers.get("Authorization", "")
        if not auth.lower().startswith("bearer "):
            return
        token = auth.split(" ", 1)[1].strip()
        try:
            payload = decode_token(token, expected_type="access")
        except Exception:
            return  # silent — deps enforce 401 where required
        set_current_org(payload.get("org_id"))
        set_current_user(payload.get("sub"))
