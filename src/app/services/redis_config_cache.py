"""Per-org ``configs`` row mirrored into Redis (design §3.2).

Avoids a Postgres round-trip on every comfort-algorithm tick (Phase 3 runs
frequently). Mirror has no TTL — it's authoritative until explicitly
invalidated by the admin-update path (Phase 5), keeping cache and source of
truth trivially consistent (load-on-miss + explicit invalidate, no polling).
"""

import uuid

from sqlalchemy.ext.asyncio import AsyncSession

from app.core.exceptions import NotFoundError
from app.core.redis_client import coerce_hash, get_redis, redis_key
from app.models.config import Config

_CFG_SUFFIX = "cfg"

_CFG_FIELDS = (
    "ema_alpha",
    "deadband",
    "dwell_sec",
    "dry_rh",
    "humid_slope",
    "clamp_min",
    "clamp_max",
    "override_hours",
    "night_start",
    "night_end",
    "night_offset",
)


async def get_cfg(org: str, session: AsyncSession) -> dict:
    """Return the tunable-constants dict for ``org``, mirroring PG on miss."""
    r = get_redis()
    key = redis_key(org, _CFG_SUFFIX)
    cached = await r.hgetall(key)
    if cached:
        return coerce_hash(cached)

    cfg = await session.get(Config, uuid.UUID(org))
    if cfg is None:
        raise NotFoundError("Config not found for organization")

    data = {field: getattr(cfg, field) for field in _CFG_FIELDS}
    await r.hset(key, mapping={k: str(v) for k, v in data.items()})
    return data


async def invalidate_cfg(org: str) -> None:
    """Drop the mirror; next :func:`get_cfg` reloads from Postgres.

    Call this from the admin config-update endpoint (Phase 5).
    """
    r = get_redis()
    await r.delete(redis_key(org, _CFG_SUFFIX))
