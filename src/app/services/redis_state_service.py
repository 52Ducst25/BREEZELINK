"""Real-time indoor/outdoor telemetry state + EMA/dwell persistence (Redis).

Hot/ephemeral state only (design §3.2) — Postgres (``telemetry`` table) holds
history. Outdoor state carries a long TTL (1200s) to tolerate deep-sleep
outdoor nodes; ``is_outdoor_stale`` is the stale-guard the comfort worker
(Phase 3) checks before trusting live outdoor readings, falling back to
``tout_ema`` when stale — never hangs on a dead node.

``tout_ema`` and ``last_switch`` are persisted with NO ttl so the running
mean / dwell-time enforcement survive a backend restart.
"""

from datetime import datetime, timezone

from app.core.redis_client import coerce_hash, get_redis, redis_key

_INDOOR_SUFFIX = "state:indoor"
_OUTDOOR_SUFFIX = "state:outdoor"
_TOUT_EMA_SUFFIX = "tout_ema"
_LAST_SWITCH_SUFFIX = "last_switch"


def _utc_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


async def _set_state(org: str, suffix: str, data: dict, ttl: int) -> None:
    r = get_redis()
    key = redis_key(org, suffix)
    payload = {**data, "updated_at": _utc_iso()}
    await r.hset(key, mapping={k: str(v) for k, v in payload.items()})
    await r.expire(key, ttl)


async def _get_state(org: str, suffix: str) -> dict | None:
    r = get_redis()
    data = await r.hgetall(redis_key(org, suffix))
    return coerce_hash(data) if data else None


async def set_indoor_state(org: str, data: dict, ttl: int = 90) -> None:
    """Store latest indoor telemetry+state hash (``{t,h,mode,setpoint}``)."""
    await _set_state(org, _INDOOR_SUFFIX, data, ttl)


async def get_indoor_state(org: str) -> dict | None:
    return await _get_state(org, _INDOOR_SUFFIX)


async def set_outdoor_state(org: str, data: dict, ttl: int = 1200) -> None:
    """Store latest outdoor telemetry hash (``{t,h}``), long TTL for deep-sleep."""
    await _set_state(org, _OUTDOOR_SUFFIX, data, ttl)


async def get_outdoor_state(org: str) -> dict | None:
    return await _get_state(org, _OUTDOOR_SUFFIX)


async def is_outdoor_stale(org: str) -> bool:
    """True if the outdoor state key is missing or expired (stale-guard)."""
    r = get_redis()
    exists = await r.exists(redis_key(org, _OUTDOOR_SUFFIX))
    return exists == 0


async def get_tout_ema(org: str) -> float | None:
    """Read the persisted running-mean outdoor temperature (no TTL)."""
    r = get_redis()
    value = await r.get(redis_key(org, _TOUT_EMA_SUFFIX))
    return float(value) if value is not None else None


async def set_tout_ema(org: str, value: float) -> None:
    """Persist the running-mean outdoor temperature (survives restart)."""
    r = get_redis()
    await r.set(redis_key(org, _TOUT_EMA_SUFFIX), str(value))


async def get_last_switch(org: str) -> tuple[str, float] | None:
    """Read ``(mode, epoch_ts)`` of the last mode switch, for DWELL enforcement."""
    r = get_redis()
    raw = await r.get(redis_key(org, _LAST_SWITCH_SUFFIX))
    if raw is None:
        return None
    mode, _, ts = raw.partition(":")
    return mode, float(ts)


async def set_last_switch(org: str, mode: str, ts: float) -> None:
    """Persist the last mode switch as ``mode:ts`` (no TTL)."""
    r = get_redis()
    await r.set(redis_key(org, _LAST_SWITCH_SUFFIX), f"{mode}:{ts}")
