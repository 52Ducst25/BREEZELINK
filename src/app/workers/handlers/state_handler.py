"""``indoor/state`` handler — ACK matching, state mirror, IR cache repair
(design §2.1/§2.2, phase-04 step 6).

The indoor node republishes this (retained) topic whenever it actuates,
carrying back the ``req_id`` it just acted on plus its resulting mode/setpoint
— the ground truth the comfort loop compares against on the next tick.

It also carries the opposite signal: ``need_raw`` (NOT retained), meaning "you
sent me an ir_code_id I do not have". That one repairs the IR cache instead of
recording success — see the branch below.
"""

import logging
from datetime import datetime, timezone

from app.core.database import AsyncSessionLocal
from app.core.tenant import set_current_org
from app.services import (
    command_service,
    ir_service,
    redis_ir_cache,
    redis_state_service,
    telemetry_service,
)
from app.utils.mqtt_naming import ParsedTopic

logger = logging.getLogger("aircon.worker.state")


async def handle_state(client, topic: ParsedTopic, payload: dict) -> None:
    """Match ``payload['ack']`` to a dispatched command and mirror state."""
    async with AsyncSessionLocal() as session:
        set_current_org(topic.org_id)

        # This mirrors mode/setpoint into the org's live state, which the comfort
        # loop then treats as ground truth — so verify the publishing node really
        # belongs to the org in the topic before trusting a word of it.
        if await telemetry_service.get_device_for_topic(session, topic.org_id, topic.device_uuid) is None:
            logger.warning("Ignoring state from unknown/mismatched uuid=%s org=%s",
                           topic.device_uuid, topic.org_id)
            return

        # The node could not resolve an ir_code_id we believed it had cached.
        # Drop the cache entry so the NEXT command for that code carries the raw
        # timing again — the node has no other way back.
        #
        # This is the return path for a desync that used to need a human: the
        # node logged "id not in NVS", refused to ack, and stayed dead until
        # someone flushed Redis by hand. Causes are ordinary — erase_flash, a
        # replaced board reusing the DEVICE_UUID, or the user deleting the code
        # from the panel's settings screen.
        #
        # Deliberately BEFORE the ack branch and independent of it: this message
        # reports a command that was NOT executed, so there is nothing to ack,
        # and the node sends no "ack" field with it.
        need_raw = payload.get("need_raw")
        if need_raw:
            dropped = await redis_ir_cache.drop_ir(str(need_raw))
            logger.info(
                "Node asked to re-send ir_code_id=%s org=%s uuid=%s (cache %s)",
                need_raw, topic.org_id, topic.device_uuid,
                "dropped" if dropped else "was already absent",
            )

        # Người dùng bấm "XIN MÃ" trên panel: đẩy lại TOÀN BỘ kho mã của org
        # xuống node. Khác `need_raw` ở chỗ đó là sửa một mã lẻ khi phát hiện
        # lệch, còn đây là khôi phục cả kho theo yêu cầu — node không thể tự hỏi
        # theo id vì nó không biết id của những mã nó đang thiếu.
        if payload.get("resync"):
            sent = await ir_service.push_all_codes(
                client, session, topic.org_id, topic.device_uuid
            )
            logger.info(
                "Resync IR org=%s uuid=%s -> da day %d ma",
                topic.org_id, topic.device_uuid, sent,
            )

        ack_req_id = payload.get("ack")
        if ack_req_id:
            matched = await command_service.set_ack(session, ack_req_id, datetime.now(timezone.utc))
            if not matched:
                logger.info("No command matched ack req_id=%s org=%s", ack_req_id, topic.org_id)

        mode, setpoint = payload.get("mode"), payload.get("setpoint")
        if mode is not None and setpoint is not None:
            await redis_state_service.set_indoor_state(topic.org_id, {"mode": mode, "setpoint": setpoint})
