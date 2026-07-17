"""IR remote-code cache — avoids a Postgres hit per manual/auto command.

Global key ``bl:ircache:{code_id}`` (no org prefix): an ``ir_codes`` row is
looked up by its own primary key, already org-scoped at write time, so the
cache key doesn't need to repeat the org.
"""

import json

from app.core.redis_client import get_redis

_RAW_TIMING_FIELD = "raw_timing"


def _cache_key(code_id: str) -> str:
    return f"bl:ircache:{code_id}"


async def put_ir(code_id: str, payload: dict) -> None:
    """Mirror an ``IrCode`` row; ``raw_timing`` (int[]) is JSON-encoded."""
    r = get_redis()
    encoded: dict[str, str] = {}
    for field, value in payload.items():
        if value is None:
            continue
        if field == _RAW_TIMING_FIELD:
            encoded[field] = json.dumps(value)
        elif isinstance(value, bytes):
            encoded[field] = value.hex()
        else:
            encoded[field] = str(value)
    if encoded:
        await r.hset(_cache_key(code_id), mapping=encoded)


async def get_ir(code_id: str) -> dict | None:
    """Return the cached IR payload, decoding ``raw_timing`` back to int[]."""
    r = get_redis()
    data = await r.hgetall(_cache_key(code_id))
    if not data:
        return None
    if _RAW_TIMING_FIELD in data:
        data[_RAW_TIMING_FIELD] = json.loads(data[_RAW_TIMING_FIELD])
    return data
