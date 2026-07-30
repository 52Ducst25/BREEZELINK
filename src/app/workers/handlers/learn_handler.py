"""LEARN-mode raw IR capture handler (design §4.2 "Học nút này", phase-04
step 8). App puts the indoor node into LEARN for a given (mode, temp); the
node captures the real remote's raw timing and republishes it here so the
backend persists it without ever needing a firmware reflash.
"""

import logging

from app.core.database import AsyncSessionLocal
from app.core.tenant import set_current_org
from app.models.enums import AcMode
from app.services import ir_action_service, ir_code_service, telemetry_service
from app.utils.mqtt_naming import ParsedTopic

logger = logging.getLogger("breezelink.worker.learn")


async def handle_learn(client, topic: ParsedTopic, payload: dict) -> None:
    """Upsert one learned ``ir_codes`` row.

    C1 fix: this must NOT prime ``redis_ir_cache`` here. The node only
    *publishes* raw timing during LEARN — it never writes its own NVS at
    this point (the code UUID is assigned server-side by
    ``upsert_learned_code`` and is unknown to the node). If we primed the
    cache now, ``command_publisher._resolve_ir_raw`` would see the code as
    "already on the node" and omit ``ir_raw`` from every command, including
    the very first one — leaving the node with an ``ir_code_id`` it cannot
    resolve and a dead LEARN->replay flow. The cache must only be marked
    once the backend has actually sent ``ir_raw`` down to the node (done by
    ``command_publisher`` right after a successful publish).
    """
    raw_timing = [int(v) for v in payload["raw_timing"]]

    # Standalone action codes (e.g. FAN_SPEED) live in their own table and are
    # NOT part of the (mode, temp) comfort matrix. The node echoes the LEARN
    # label it was given; accept it under either "action" or "mode" so this
    # works whether the firmware treats the label as an action or as a "mode".
    label = payload.get("action") or payload.get("mode")
    if label in ir_action_service.KNOWN_ACTIONS:
        async with AsyncSessionLocal() as session:
            set_current_org(topic.org_id)
            # Learned codes are what the household's AC is later driven with —
            # never accept them from a node that isn't actually in this org.
            if await telemetry_service.get_device_for_topic(session, topic.org_id, topic.device_uuid) is None:
                logger.warning("Ignoring LEARN action from unknown/mismatched uuid=%s org=%s",
                               topic.device_uuid, topic.org_id)
                return
            await ir_action_service.upsert_action_code(session, topic.org_id, label, raw_timing)
        logger.info("Learned IR action org=%s action=%s", topic.org_id, label)
        return

    mode = AcMode(payload["mode"])
    # M3 fix: fixed modes (DRY/FAN/OFF) have no setpoint, so the node's LEARN
    # echo omits "temp" for them — treat it as optional instead of a hard
    # KeyError (upsert_learned_code normalizes missing temp to the shared
    # fixed-mode sentinel; COOL still requires a real temp).
    temp = int(payload["temp"]) if payload.get("temp") is not None else None

    async with AsyncSessionLocal() as session:
        set_current_org(topic.org_id)
        if await telemetry_service.get_device_for_topic(session, topic.org_id, topic.device_uuid) is None:
            logger.warning("Ignoring LEARN code from unknown/mismatched uuid=%s org=%s",
                           topic.device_uuid, topic.org_id)
            return
        ir_code = await ir_code_service.upsert_learned_code(session, topic.org_id, mode, temp, raw_timing)

    logger.info("Learned IR code org=%s mode=%s temp=%s id=%s", topic.org_id, mode.value, temp, ir_code.id)
