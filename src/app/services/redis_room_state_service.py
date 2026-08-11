"""Latest sample from every room-corner sensor of one household (Redis).

ONE HASH PER ORG, NOT ONE KEY PER SENSOR — ``bl:{org}:state:rooms`` with the
device id as the field. The obvious alternative (a key per sensor plus a TTL,
mirroring ``state:indoor``) means a ``SCAN`` on every telemetry tick to find
them again, and four sensors at 15 s each is 16 scans a minute per household on
the hot compute path. One ``HGETALL`` answers the same question in one round
trip, and it stays one round trip whether the room has four sensors or ten.

The price of dropping per-key TTLs is that a decommissioned sensor's field would
linger forever, so freshness is carried IN the value (``at``, a unix epoch) and
judged by ``comfort/room_aggregate.aggregate``; ``prune`` drops fields that have
been silent long past any plausible outage. The whole hash still carries a long
expire so a deleted household leaves nothing behind.

Storing the epoch rather than relying on key expiry is also what makes the state
honest: with a TTL, a sensor that died 100 s ago and one that reported 1 s ago
are indistinguishable until the key vanishes. Here the age is a number the
caller can act on and log.
"""

import json
import logging
import time

from app.comfort.room_aggregate import RoomAggregate, RoomReading, aggregate
from app.core.redis_client import get_redis, redis_key

logger = logging.getLogger("breezelink.redis_room_state")

_ROOMS_SUFFIX = "state:rooms"

# A sensor silent this long stops counting toward the indoor reading. Six missed
# 30 s ticks: long enough that a burst of BLE interference or a gateway
# reconnect does not drop a healthy corner out of the median, short enough that
# a corner that actually lost power stops steering the air conditioner within
# minutes rather than hours.
ROOM_MAX_AGE_SEC = 180.0

# How long a silent sensor's field survives in the hash at all. Well beyond
# ROOM_MAX_AGE_SEC on purpose: a field that is stale but present is what lets
# the UI say "corner 3 last seen 40 minutes ago" instead of forgetting the
# sensor ever existed, which reads as "there were only three sensors".
_PRUNE_AFTER_SEC = 6 * 3600

# Expiry on the hash itself, refreshed on every write. Only fires for a
# household with no sensor reporting for a full day — i.e. one that is gone.
_HASH_TTL_SEC = 24 * 3600


async def set_room_state(org: str, device_id: str, temp: float, humidity: float) -> None:
    """Record one corner sensor's latest reading, stamped with the receive time."""
    r = get_redis()
    key = redis_key(org, _ROOMS_SUFFIX)
    payload = json.dumps({"t": temp, "h": humidity, "at": time.time()})
    await r.hset(key, str(device_id), payload)
    await r.expire(key, _HASH_TTL_SEC)


async def list_room_states(org: str) -> list[RoomReading]:
    """Every known corner sensor with its age in seconds, freshest first.

    Unparseable fields are skipped with a warning rather than raising: this sits
    on the comfort path, and one corrupt value must not stop the other three
    sensors from driving the household.
    """
    r = get_redis()
    raw = await r.hgetall(redis_key(org, _ROOMS_SUFFIX))
    now = time.time()
    readings: list[RoomReading] = []
    for device_id, blob in (raw or {}).items():
        try:
            data = json.loads(blob)
            readings.append(
                RoomReading(
                    device_id=device_id,
                    temp=float(data["t"]),
                    humidity=float(data["h"]),
                    age_sec=max(now - float(data["at"]), 0.0),
                )
            )
        except (ValueError, TypeError, KeyError):
            logger.warning("Unreadable room state org=%s device=%s — skipped", org, device_id)
    readings.sort(key=lambda x: x.age_sec)
    return readings


async def aggregate_rooms(org: str, max_age_sec: float = ROOM_MAX_AGE_SEC) -> RoomAggregate | None:
    """Median of the fresh corner sensors, or None when none is fresh."""
    return aggregate(await list_room_states(org), max_age_sec=max_age_sec)


async def prune(org: str, older_than_sec: float = _PRUNE_AFTER_SEC) -> int:
    """Drop long-silent sensors from the hash. Returns how many were removed."""
    r = get_redis()
    key = redis_key(org, _ROOMS_SUFFIX)
    dead = [x.device_id for x in await list_room_states(org) if x.age_sec > older_than_sec]
    if dead:
        await r.hdel(key, *dead)
    return len(dead)


async def forget(org: str, device_id: str) -> None:
    """Remove one sensor outright — call when its device row is deleted, so a
    decommissioned corner stops appearing in diagnostics forever."""
    r = get_redis()
    await r.hdel(redis_key(org, _ROOMS_SUFFIX), str(device_id))
