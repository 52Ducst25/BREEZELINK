"""``override`` handler — the node's panel asking to take (or hand back) the
wheel (Interface/README.md §8.3).

Before this existed the touch panel could only fake an override: it blasted the
IR frame and mirrored mode/setpoint through ``state``, but the manual gate lives
in Redis and was reachable ONLY over REST. So the comfort loop kept deciding,
and roughly one telemetry tick later it published a ``cmd`` that overwrote
whatever the user had just dialled in on the wall. The panel said "máy chủ sẽ
giành lại quyền" because that was the truth. This handler is the missing half.

TWO THINGS THIS DELIBERATELY DOES NOT DO:

1. It does NOT publish a ``cmd`` back to the node. The panel already blasted the
   IR frame itself before publishing here (``runPanelCommand``), so a command
   would be a second blast of a frame the AC just received — and, worse, the
   node clears its local override badge on every inbound ``cmd``, so the very
   act of honouring the panel's request would make the panel claim the server
   had taken over again.
2. It does NOT write ``redis_state``. The node publishes ``state`` separately
   for exactly that, and duplicating the mirror here would mean two writers
   racing over one key with no ordering guarantee between two MQTT messages.
"""

import json
import logging

from app.core.database import AsyncSessionLocal
from app.core.tenant import set_current_org
from app.models.enums import AcMode
from app.services import (
    live_events,
    redis_config_cache,
    redis_override_service,
    telemetry_service,
)
from app.utils.mqtt_naming import ParsedTopic

logger = logging.getLogger("breezelink.worker.override")


async def handle_override(client, topic: ParsedTopic, payload: dict) -> None:
    """Arm or clear the org's manual-override gate on the panel's request."""
    async with AsyncSessionLocal() as session:
        set_current_org(topic.org_id)

        # This gates the whole household's auto-control, so verify the publisher
        # really belongs to the org in the topic before acting on a word of it —
        # same guard as every other device -> cloud handler.
        if await telemetry_service.get_device_for_topic(session, topic.org_id, topic.device_uuid) is None:
            logger.warning("Ignoring override from unknown/mismatched uuid=%s org=%s",
                           topic.device_uuid, topic.org_id)
            return

        # "TỰ ĐỘNG" on the panel. Checked first and independently of mode/setpoint:
        # handing control back is not a command about any particular temperature,
        # and the node sends no mode/setpoint with it.
        if payload.get("clear"):
            await redis_override_service.clear_override(topic.org_id)
            logger.info("Panel released override org=%s uuid=%s", topic.org_id, topic.device_uuid)
            await live_events.publish_change(topic.org_id)
            return

        raw_mode, setpoint = payload.get("mode"), payload.get("setpoint")
        if raw_mode is None or setpoint is None:
            logger.warning("Override without mode/setpoint org=%s payload=%s", topic.org_id, payload)
            return

        # Validate before it reaches Redis. The gate's value is read back by
        # clients as the active manual command, so a typo'd mode from a
        # half-flashed node would surface in the app as a real setting.
        try:
            mode = AcMode(raw_mode)
            setpoint = int(setpoint)
        except (ValueError, TypeError):
            logger.warning("Override with invalid mode/setpoint org=%s payload=%s", topic.org_id, payload)
            return

        cfg = await redis_config_cache.get_cfg(topic.org_id, session)
        hours = int(cfg["override_hours"])

    # Same TTL the app's REST override uses. An override from the wall and an
    # override from the phone are the same user intent, so they get the same
    # lifetime — and the panel has its own "TỰ ĐỘNG" button to end it early.
    await redis_override_service.set_override(
        topic.org_id,
        json.dumps({"mode": mode.value, "setpoint": setpoint}),
        hours=hours,
    )
    logger.info("Panel took override org=%s uuid=%s mode=%s setpoint=%s ttl=%sh",
                topic.org_id, topic.device_uuid, mode.value, setpoint, hours)
    # Push it so the app's badge flips now instead of at the next telemetry tick.
    await live_events.publish_change(topic.org_id)
