"""Config read/update for the admin ``/configs`` API (Phase 5).

``update_config`` re-checks clamp_min<clamp_max against the MERGED row
(existing DB values + incoming partial update) — the schema-level validator
(``ConfigUpdate``) only catches the case where both bounds are sent in the
same request; a request that touches only one bound could otherwise violate
the invariant against whatever is already persisted.
"""

import uuid

from sqlalchemy.ext.asyncio import AsyncSession

from app.core.exceptions import AppException, NotFoundError
from app.models.config import Config
from app.schemas.config import ConfigUpdate
from app.services import redis_config_cache


# Canonical default comfort constants for a newly-created org (design §1.2/§1.3).
# Mirrors the demo-org seed in migration 260714_2105 — applied at org-creation
# time so every org is immediately usable (comfort worker has knobs; the admin
# /admin/config page renders instead of 404-ing on a missing row).
DEFAULT_CONFIG: dict = {
    "ema_alpha": 0.2,
    "deadband": 0.8,
    "dwell_sec": 600,
    "dry_rh": 72.0,
    "humid_slope": 0.05,
    "clamp_min": 24.0,
    "clamp_max": 28.0,
    "override_hours": 2,
    "night_start": 23,
    "night_end": 6,
    "night_offset": 0.5,
}


async def create_default_config(session: AsyncSession, org_id: uuid.UUID) -> Config:
    """Create the org's initial comfort-config row from ``DEFAULT_CONFIG``.

    Called at registration so a brand-new org has a usable config immediately.
    Does NOT commit — the caller owns the surrounding transaction (register()
    commits org + owner + config atomically).
    """
    cfg = Config(org_id=org_id, **DEFAULT_CONFIG)
    session.add(cfg)
    return cfg


async def get_config(session: AsyncSession, org_id: uuid.UUID) -> Config:
    cfg = await session.get(Config, org_id)
    if cfg is None:
        raise NotFoundError("Config not found for organization")
    return cfg


async def update_config(session: AsyncSession, org_id: uuid.UUID, data: ConfigUpdate) -> Config:
    """Apply the partial update, validate the merged row, persist, and
    invalidate the Redis mirror so the comfort worker reloads fresh values.
    """
    cfg = await get_config(session, org_id)
    for field, value in data.model_dump(exclude_unset=True, exclude_none=True).items():
        setattr(cfg, field, value)

    if cfg.clamp_min >= cfg.clamp_max:
        await session.rollback()
        raise AppException("clamp_min must be less than clamp_max", status_code=422)

    await session.commit()
    await redis_config_cache.invalidate_cfg(str(org_id))
    return cfg
