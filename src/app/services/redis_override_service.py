"""Manual-override gate (Redis TTL) — design §3.2 / Bước E.

When a user issues a manual command the app sets an override key with a TTL
of ``override_hours`` (from ``configs``); while it's active the comfort
worker (Phase 3) must skip automatic decisions. The key auto-expires and
control silently resumes — no explicit "resume" action needed.
"""

from app.core.redis_client import get_redis, redis_key

_OVERRIDE_SUFFIX = "override"


async def set_override(org: str, cmd_json: str, hours: int) -> None:
    """Set the override gate: ``SET bl:{org}:override <cmd_json> EX hours*3600``."""
    r = get_redis()
    await r.set(redis_key(org, _OVERRIDE_SUFFIX), cmd_json, ex=hours * 3600)


async def get_override(org: str) -> str | None:
    """Return the raw override command JSON, or None if not active."""
    r = get_redis()
    return await r.get(redis_key(org, _OVERRIDE_SUFFIX))


async def override_active(org: str) -> bool:
    """True while a manual override gate is set (auto-control must skip)."""
    r = get_redis()
    return bool(await r.exists(redis_key(org, _OVERRIDE_SUFFIX)))


async def clear_override(org: str) -> None:
    """Explicitly clear the override gate (e.g. user resumes auto early)."""
    r = get_redis()
    await r.delete(redis_key(org, _OVERRIDE_SUFFIX))
