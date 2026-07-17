"""LWT ``status`` handler — device online/offline presence (phase-04 step 7).

Payload is a plain literal string (``"online"``/``"offline"``), NOT JSON —
this is the broker-generated Last Will payload, so the consumer dispatch
loop hands it over undecoded (see ``mqtt_consumer._dispatch``).
"""

import logging

from app.core.database import AsyncSessionLocal
from app.core.tenant import set_current_org
from app.models.base import utcnow
from app.models.device import DeviceStatus
from app.services import live_events, telemetry_service
from app.utils.mqtt_naming import ParsedTopic

logger = logging.getLogger("aircon.worker.status")

_VALID = {"online": DeviceStatus.online, "offline": DeviceStatus.offline}


async def handle_status(client, topic: ParsedTopic, payload: str) -> None:
    """Update ``devices.status`` + ``last_seen_at`` from an LWT/presence msg."""
    status = _VALID.get(payload.strip().lower())
    if status is None:
        logger.warning("Unrecognized status payload %r on org=%s node=%s", payload, topic.org_id, topic.node)
        return

    async with AsyncSessionLocal() as session:
        set_current_org(topic.org_id)
        device = await telemetry_service.get_device_by_org_and_node(session, topic.org_id, topic.node)
        if device is None:
            logger.warning("No device registered for org=%s node=%s", topic.org_id, topic.node)
            return
        device.status = status
        device.last_seen_at = utcnow()
        await session.commit()

    # Push the online/offline change to the realtime feed immediately.
    await live_events.publish_change(topic.org_id)
