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


async def drop_ir(code_id: str) -> bool:
    """Forget that a node holds this code, so the next command re-sends ``ir_raw``.

    This entry means "the node has it in NVS" — ``command_publisher`` omits the
    raw timing whenever it is present. When the node says otherwise (it was
    erased, swapped, or the user deleted the code from the panel), the entry is
    a lie that makes every future command for that code a no-op: the node gets
    an id it cannot resolve and silently does nothing.

    Deleting is always safe. Worst case the next command carries a few KB of
    timing the node already had — cheap next to an air conditioner that stops
    responding with nothing in the logs.

    Returns True if an entry was actually removed.
    """
    return bool(await get_redis().delete(_cache_key(code_id)))
