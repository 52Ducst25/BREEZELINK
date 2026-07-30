"""Realtime state-change fan-out over Redis pub/sub for the WebSocket live feed.

The worker publishes a lightweight nudge on ``bl:{org}:events`` whenever it updates
an org's live state (telemetry, comfort decision, device online/offline). The API's
WebSocket endpoint (``ws_routes``) subscribes to that channel and re-pushes a fresh
snapshot to connected clients — decoupling the worker (publisher) from the API
(subscriber), which run in separate processes.

Best-effort by design: a pub/sub hiccup must never break the worker's control loop,
so ``publish_change`` swallows errors.
"""

import logging

from app.core.redis_client import get_redis

logger = logging.getLogger("breezelink.live_events")


# One channel every vendor-staff socket listens on, in addition to their own
# org's. Needed because the interesting event for the back office happens in a
# DIFFERENT org from the viewer's: a customer redeems a code and an account
# appears in *their* household. An org-scoped nudge can never reach staff, so
# the customer list would sit stale until someone hit refresh.
#
# Not a per-staff channel: every staff member wants the same news, and there is
# no per-recipient state to keep.
VENDOR_CHANNEL = "bl:vendor:events"


def channel(org_id: str) -> str:
    return f"bl:{org_id}:events"


async def publish_change(org_id: str, kind: str = "state") -> None:
    """Nudge subscribers that ``org_id``'s live state changed. Never raises."""
    try:
        await get_redis().publish(channel(org_id), kind)
    except Exception as exc:  # realtime is best-effort — don't break the worker
        logger.debug("publish_change failed org=%s: %s", org_id, exc)


async def publish_vendor_change(kind: str = "customers") -> None:
    """Nudge vendor-staff sockets that the customer list changed. Never raises.

    Same best-effort contract as publish_change: a Redis hiccup must not fail
    the registration that triggered it — the customer's account is created
    either way, the vendor's page just refreshes a beat later.
    """
    try:
        await get_redis().publish(VENDOR_CHANNEL, kind)
    except Exception as exc:
        logger.debug("publish_vendor_change failed: %s", exc)
