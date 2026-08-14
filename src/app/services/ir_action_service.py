"""``ir_action_codes`` reads/writes — standalone learned actions (e.g. the
remote's "fan speed" button) that live outside the (mode, temp) comfort matrix.

Deliberately separate from ``ir_code_service`` so an action code can never be
snapped/looked-up as a comfort code, and so the LEARN handler + the fan-speed
send endpoint have one clear place for this table's access.
"""

import uuid

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.ir_action_code import IrActionCode

# Standalone remote buttons outside the (mode, temp) matrix — each is a single
# learned IR frame, blasted on demand. The LEARN handler recognises these labels
# on the node's echo and rejects anything else, and the send/learn routes
# validate against this set. Adding a new button = one entry here (+ its label
# and icon in the Flutter AcAction). FAN_SPEED is first for back-compat with the
# already-shipped /comfort/fan-speed endpoint (build 6).
FAN_SPEED = "FAN_SPEED"

#  Các mức quạt ĐẶT THẲNG — mỗi mức là MỘT khung IR học riêng, y hệt cách
#  COOL 24..28 mỗi nhiệt độ một mã.
#
#  VÌ SAO KHÔNG TÁI DÙNG FAN_SPEED: nút đó mô phỏng nút "quạt" trên remote, tức
#  là NHẢY SANG MỨC KẾ TIẾP theo vòng của chính máy. Một mã duy nhất thì không
#  có cách nào tới thẳng mức 60% — chỉ bấm nhiều lần rồi đoán, mà đoán sai vì
#  không ai biết máy đang ở mức nào (xem chú thích "không theo dõi" bên dưới).
#
#  Remote điều hoà gửi TRỌN trạng thái trong mỗi khung, không gửi lệnh "tăng
#  một nấc" — nên chỉnh remote tới đúng mức rồi học, sẽ ra một khung mã hoá
#  đúng mức đó. Đó là lý do cách này khả thi.
#
#  NHÃN PHẦN TRĂM LÀ QUY ƯỚC CỦA MÌNH: remote thật thường chỉ có Low/Med/High
#  (+Turbo/Auto), hiếm khi đủ 5 nấc. Máy nào ít nấc hơn thì org đó chỉ học được
#  vài mức, các mức còn lại vẫn hiện nhưng máy chủ trả "chưa học" khi bấm.
FAN_LEVELS = ("FAN_20", "FAN_40", "FAN_60", "FAN_80", "FAN_100", "FAN_AUTO")

#  MÁY TẠO ĐỘ ẨM — hai ô mã, và panel tự lái nó bằng độ ẩm trong phòng.
#
#  Ở ĐÂY, KHÔNG PHẢI MỘT BẢNG RIÊNG: đây vẫn đúng là "một khung IR học được,
#  phát lại nguyên văn" — cùng khuôn với mọi nút rời khác, nên dùng chung luồng
#  học/xoá/liệt kê đã có thay vì dựng một đường song song chỉ khác cái tên.
#
#  MÁY TẠO ẨM KHÔNG ĐI QUA MÁY CHỦ lúc chạy: panel giữ mã trong NVS, đo độ ẩm
#  bằng trung vị 4 node góc phòng, và tự quyết định bật/tắt. Backend chỉ tham gia
#  ở khâu HỌC MÃ và ở khâu đẩy lại mã khi panel xin (`push_all_codes`). Nối vòng
#  điều khiển lên cloud chỉ đổi được đúng một thứ: nó sẽ ngừng chạy khi rớt mạng.
#
#  HAI Ô, KHÔNG PHẢI MỘT: rất nhiều remote máy xông chỉ có một nút nguồn bập
#  bênh. Hộ nào như vậy thì học cùng một khung vào cả hai ô — panel tự nhận ra và
#  chuyển sang chế độ bập bênh (xem humidifier-control.cpp). Hộ nào remote có nút
#  BẬT và nút TẮT rời nhau thì hai ô mang hai khung khác nhau, và bắn lại vô hại.
HUMID_ON = "HUMID_ON"
HUMID_OFF = "HUMID_OFF"

KNOWN_ACTIONS = frozenset({
    FAN_SPEED,   # nút vòng của remote — GIỮ LẠI cho org đã học từ trước
    *FAN_LEVELS,
    HUMID_ON,    # máy tạo độ ẩm: bật
    HUMID_OFF,   # máy tạo độ ẩm: tắt
    "SUPER",     # turbo / siêu tốc
    "SLEEP",     # chế độ ngủ
    "ECO",       # tiết kiệm điện
    "QUIET",     # yên tĩnh
    "SMART",     # tự động thông minh
    "TIMER",     # hẹn giờ
    "SWING_V",   # đảo gió dọc (louver lên/xuống)
    "SWING_H",   # đảo gió ngang
    "LIGHT",     # bật/tắt đèn màn hình dàn lạnh
    "TEMP_UNIT", # đổi đơn vị °C/°F
})


async def get_action_raw(session: AsyncSession, org_id: str, action: str) -> list[int] | None:
    """Raw mark/space timing for one org's action code, or ``None`` if unlearned."""
    stmt = select(IrActionCode.raw_timing).where(
        IrActionCode.org_id == uuid.UUID(org_id), IrActionCode.action == action
    )
    return (await session.execute(stmt)).scalar_one_or_none()


async def list_actions(session: AsyncSession, org_id: uuid.UUID) -> list[str]:
    """Action labels this org has learned, e.g. ``["FAN_SPEED"]``."""
    stmt = select(IrActionCode.action).where(IrActionCode.org_id == org_id)
    return list((await session.execute(stmt)).scalars().all())


async def delete_action(session: AsyncSession, org_id: uuid.UUID, action: str) -> bool:
    """Xoá mã của một nút chức năng. Trả False nếu org này chưa học nút đó."""
    stmt = select(IrActionCode).where(
        IrActionCode.org_id == org_id, IrActionCode.action == action
    )
    row = (await session.execute(stmt)).scalar_one_or_none()
    if row is None:
        return False
    await session.delete(row)
    await session.commit()
    return True


async def upsert_action_code(
    session: AsyncSession, org_id: str, action: str, raw_timing: list[int]
) -> IrActionCode:
    """Insert or replace the ``(org, action)`` row from a LEARN capture."""
    stmt = select(IrActionCode).where(
        IrActionCode.org_id == uuid.UUID(org_id), IrActionCode.action == action
    )
    existing = (await session.execute(stmt)).scalar_one_or_none()
    if existing is not None:
        existing.raw_timing = raw_timing
        await session.commit()
        return existing

    row = IrActionCode(org_id=uuid.UUID(org_id), action=action, raw_timing=raw_timing)
    session.add(row)
    await session.commit()
    return row
