"""Retained ``indoor/state`` handler — app-level ACK matching + state mirror
(design §2.1/§2.2, phase-04 step 6).

The indoor node republishes this (retained) topic whenever it actuates,
carrying back the ``req_id`` it just acted on plus its resulting mode/setpoint
— the ground truth the comfort loop compares against on the next tick.
"""

import logging
from datetime import datetime, timezone

from app.core.database import AsyncSessionLocal
from app.core.tenant import set_current_org
from app.services import command_service, redis_state_service
from app.utils.mqtt_naming import ParsedTopic

logger = logging.getLogger("aircon.worker.state")


async def handle_state(client, topic: ParsedTopic, payload: dict) -> None:
    """Match ``payload['ack']`` to a dispatched command and mirror state."""
    async with AsyncSessionLocal() as session:
        set_current_org(topic.org_id)

        ack_req_id = payload.get("ack")
        if ack_req_id:
            matched = await command_service.set_ack(session, ack_req_id, datetime.now(timezone.utc))
            if not matched:
                logger.info("No command matched ack req_id=%s org=%s", ack_req_id, topic.org_id)

        mode, setpoint = payload.get("mode"), payload.get("setpoint")
        if mode is not None and setpoint is not None:
            await redis_state_service.set_indoor_state(topic.org_id, {"mode": mode, "setpoint": setpoint})
