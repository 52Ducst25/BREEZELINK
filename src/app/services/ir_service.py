"""IR-code listing, coverage check, and LEARN-trigger publish (Phase 5).

Coverage (design §5 risk): auto-control needs a learned code for COOL at
every clamp-range temp (24-28) plus one each for DRY/FAN/OFF. Missing any
means that setpoint/mode can't actually be sent to the AC.
"""

import json
import uuid

import aiomqtt
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.enums import AcMode
from app.models.ir_code import IrCode
from app.utils import mqtt_naming

_REQUIRED_COOL_TEMPS = (24, 25, 26, 27, 28)
_REQUIRED_FIXED_MODES = (AcMode.DRY, AcMode.FAN, AcMode.OFF)


async def list_codes(session: AsyncSession, org_id: uuid.UUID) -> list[IrCode]:
    """All captured codes for one org, grouped for display by mode/temp."""
    stmt = (
        select(IrCode)
        .where(IrCode.org_id == org_id)
        .order_by(IrCode.mode, IrCode.temp)
    )
    return list((await session.execute(stmt)).scalars().all())


async def delete_code(session: AsyncSession, org_id: uuid.UUID, code_id: uuid.UUID) -> bool:
    """Xoá một mã đã học. Trả False nếu không có hàng nào khớp.

    Lọc theo CẢ org_id lẫn code_id, không chỉ code_id: id là UUID nên đoán được
    thì khó, nhưng lọc theo org là thứ duy nhất chặn được việc một tổ chức xoá
    mã của tổ chức khác nếu id rò rỉ ra ngoài.
    """
    stmt = select(IrCode).where(IrCode.org_id == org_id, IrCode.id == code_id)
    row = (await session.execute(stmt)).scalar_one_or_none()
    if row is None:
        return False
    await session.delete(row)
    await session.commit()
    return True


async def check_coverage(session: AsyncSession, org_id: uuid.UUID) -> list[str]:
    """Human-readable list of still-missing required buttons, e.g. ``["COOL 24", "DRY"]``."""
    stmt = select(IrCode.mode, IrCode.temp).where(IrCode.org_id == org_id)
    rows = (await session.execute(stmt)).all()
    captured = {(mode, temp) for mode, temp in rows}
    captured_modes = {mode for mode, _ in rows}

    missing = [
        f"COOL {temp}" for temp in _REQUIRED_COOL_TEMPS if (AcMode.COOL, temp) not in captured
    ]
    missing += [mode.value for mode in _REQUIRED_FIXED_MODES if mode not in captured_modes]
    return missing


async def trigger_learn(
    client: aiomqtt.Client, org_id: str, device_uuid: str, mode: AcMode, temp: int | None
) -> None:
    """Publish ``bl/{org}/{device_uuid}/cmd {learn: "MODE TEMP"}`` (design §4.2).

    ``device_uuid`` is the indoor node to teach (its IR receiver captures the
    real remote). The node enters LEARN, captures the next raw IR signal and
    reports it back over the ``learn`` topic (Phase 4's ``learn_handler``).
    """
    learn_value = f"{mode.value} {temp}" if temp is not None else mode.value
    topic = mqtt_naming.topic(org_id, device_uuid, "cmd")
    await client.publish(topic, json.dumps({"learn": learn_value}), qos=1, retain=False)


async def trigger_learn_action(
    client: aiomqtt.Client, org_id: str, device_uuid: str, action: str
) -> None:
    """Put the given indoor node into LEARN for a standalone action button.

    Same fire-and-forget contract as :func:`trigger_learn`, but the LEARN label
    is the action name (no mode/temp). ``learn_handler`` routes the known action
    label into ``ir_action_codes``.
    """
    topic = mqtt_naming.topic(org_id, device_uuid, "cmd")
    await client.publish(topic, json.dumps({"learn": action}), qos=1, retain=False)
