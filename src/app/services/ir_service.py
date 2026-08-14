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


async def push_all_codes(
    client: aiomqtt.Client, session: AsyncSession, org_id: str, device_uuid: str
) -> int:
    """Re-send every learned code of an org down to one node. Returns how many.

    Answers the panel's "XIN MÃ" button: restore this node's IR store from the
    server. The node cannot ask for codes by id — it does not know the ids of
    codes it is missing — so the whole set is pushed and the node overwrites in
    place.

    ``store_only`` is what makes this safe, and it is not optional. A normal
    ``cmd`` carrying ``ir_raw`` tells the node to BLAST that frame at the air
    conditioner; without the flag this loop would fire ~18 remote presses in a
    row — mode and temperature swinging wildly, ending on whatever the last row
    happened to be. With it, the node only writes NVS and stays silent.

    The Redis cache is refreshed alongside each publish so the next real command
    can go back to omitting the raw timing. Doing it here (not on ack) keeps the
    two in step: the node has the code the moment it processes the message, and
    a lost message leaves the cache claiming a code the node lacks — which the
    ``need_raw`` path already repairs.

    STANDALONE ACTION CODES GO DOWN TOO, and that is not a nicety. The panel now
    blasts fan levels and drives the humidifier BY ITSELF, straight from NVS —
    no round trip to the server, so it keeps working with the internet down.
    That only holds if the node actually has those frames, and there is no other
    way for it to get them: action codes carry no ``ir_code_id`` (see
    ``publish_ir_action``), so the ``need_raw`` repair path cannot ask for one.
    Leaving them out means a household that learned every fan level in the app
    still has a dead fan card on the wall panel after one ``erase_flash``.
    """
    from app.services import ir_action_service, redis_ir_cache  # cục bộ: tránh vòng import

    codes = await list_codes(session, uuid.UUID(org_id))
    topic = mqtt_naming.topic(org_id, device_uuid, "cmd")
    sent = 0

    # Nút rời đi TRƯỚC, có chủ đích: chúng ít (tối đa ~19) và là thứ panel không
    # có đường nào khác để xin lại, còn ma trận (mode, temp) thì lệnh kế tiếp tự
    # sửa được qua `need_raw`. Hàng đợi MQTT có thể đứt giữa chừng, nên gửi thứ
    # không cứu được bằng cách khác lên đầu.
    for action in sorted(await ir_action_service.list_actions(session, uuid.UUID(org_id))):
        raw = await ir_action_service.get_action_raw(session, org_id, action)
        if not raw:
            continue
        payload = {
            "store_only": True,
            # KHÔNG có "ir_code_id": bảng ir_action_codes khoá theo (org, action),
            # không sinh UUID. Node lưu thẳng dưới bí danh chính là tên nút.
            "action": action,
            "ir_raw": list(raw),
            "reason": "resync",
        }
        await client.publish(topic, json.dumps(payload), qos=1, retain=False)
        sent += 1

    for code in codes:
        # Mã chỉ có `state_bytes` (đường thư viện theo hãng) chưa dùng tới ở
        # firmware hiện tại — nó chỉ biết phát mảng thời gian thô.
        if not code.raw_timing:
            continue
        raw = list(code.raw_timing)
        payload = {
            "store_only": True,
            "ir_code_id": str(code.id),
            "mode": code.mode.value,
            "setpoint": int(code.temp),
            "ir_raw": raw,
            "reason": "resync",
        }
        await client.publish(topic, json.dumps(payload), qos=1, retain=False)
        await redis_ir_cache.put_ir(str(code.id), {"raw_timing": raw})
        sent += 1
    return sent


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
