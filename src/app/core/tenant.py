"""Tenant isolation via contextvars.

Middleware (API) or the worker sets the current org / user; services read them to
scope every query by ``org_id``. This keeps multi-tenant isolation at the
application layer without threading org_id through every call signature.
"""

from contextvars import ContextVar

_org_id_ctx: ContextVar[str | None] = ContextVar("org_id", default=None)
_user_id_ctx: ContextVar[str | None] = ContextVar("user_id", default=None)


def set_current_org(org_id: str | None) -> None:
    _org_id_ctx.set(org_id)


def get_current_org() -> str | None:
    return _org_id_ctx.get()


def set_current_user(user_id: str | None) -> None:
    _user_id_ctx.set(user_id)


def get_current_user_id() -> str | None:
    return _user_id_ctx.get()
