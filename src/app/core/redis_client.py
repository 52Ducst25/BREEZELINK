"""Redis connection pool accessor (async).

Activated in Phase 2 by the ``redis_*`` services under ``app/services`` for
real-time state, EMA persistence, override gate, config mirror, and IR cache.

NOTE (risk, design §3.2/Phase 2): this module does not itself guard against
Redis being down — callers that sit on the hot compute path (Phase 3/4, the
comfort worker) must wrap ``redis_*`` service calls in try/except and
degrade gracefully (e.g. keep the last known mode) rather than raise.
"""

from redis.asyncio import Redis, from_url

from app.config import get_settings

_client: Redis | None = None


def get_redis() -> Redis:
    """Return a lazily-created pooled async Redis client."""
    global _client
    if _client is None:
        _client = from_url(
            get_settings().redis_url, encoding="utf-8", decode_responses=True
        )
    return _client


def redis_key(org: str, suffix: str) -> str:
    """Build the namespaced per-org key ``bl:{org}:{suffix}`` (design §3.2).

    IR cache is the one exception (global, keyed by ``bl:ircache:{code_id}``
    directly) since remote codes aren't org-scoped lookups.
    """
    return f"bl:{org}:{suffix}"


def coerce_number(value: str) -> int | float | str:
    """Best-effort cast of a Redis string value back to int/float, else str.

    Redis hashes/strings are text-only; this undoes ``str()`` on read for
    numeric fields (temp, humidity, setpoint, ema, cfg constants) while
    leaving non-numeric fields (mode, ISO timestamps) untouched.
    """
    try:
        return int(value)
    except (TypeError, ValueError):
        pass
    try:
        return float(value)
    except (TypeError, ValueError):
        return value


def coerce_hash(data: dict[str, str]) -> dict[str, int | float | str]:
    """Apply :func:`coerce_number` to every value of a decoded Redis hash."""
    return {k: coerce_number(v) for k, v in data.items()}
