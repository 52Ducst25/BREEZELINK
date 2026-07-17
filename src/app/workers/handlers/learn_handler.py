"""LEARN-mode raw IR capture handler (design §4.2 "Học nút này", phase-04
step 8). App puts the indoor node into LEARN for a given (mode, temp); the
node captures the real remote's raw timing and republishes it here so the
backend persists it without ever needing a firmware reflash.
"""

import logging

from app.core.database import AsyncSessionLocal
from app.core.tenant import set_current_org
from app.models.enums import AcMode
from app.services import ir_code_service
from app.utils.mqtt_naming import ParsedTopic

logger = logging.getLogger("aircon.worker.learn")


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
    mode = AcMode(payload["mode"])
    # M3 fix: fixed modes (DRY/FAN/OFF) have no setpoint, so the node's LEARN
    # echo omits "temp" for them — treat it as optional instead of a hard
    # KeyError (upsert_learned_code normalizes missing temp to the shared
    # fixed-mode sentinel; COOL still requires a real temp).
    temp = int(payload["temp"]) if payload.get("temp") is not None else None
    raw_timing = [int(v) for v in payload["raw_timing"]]

    async with AsyncSessionLocal() as session:
        set_current_org(topic.org_id)
        ir_code = await ir_code_service.upsert_learned_code(session, topic.org_id, mode, temp, raw_timing)

    logger.info("Learned IR code org=%s mode=%s temp=%s id=%s", topic.org_id, mode.value, temp, ir_code.id)
