"""Bố cục gói trên đường UART tới gateway.

BẢN SONG SINH CỦA ``FirmWare/shared/unoq-link-protocol.h``. Chuỗi ``struct``
dưới đây phải khớp từng byte với struct C bên đó; đổi một bên mà quên bên kia
thì gói vẫn giải mã "thành công" và cho ra nhiệt độ rác — nên có ``VERSION``, và
nó được kiểm ở mọi lối vào.

Vì sao không sinh tự động từ header C: một bộ sinh mã là thêm một bước build
phải chạy đúng trên cả máy dev lẫn UNO Q, để tránh chép hai chục dòng. Cái giá
đó không đáng; cái giá của việc quên đồng bộ thì được chặn bằng version + CRC.
"""

import struct
from dataclasses import dataclass
from enum import IntEnum

MAGIC = 0xAC
# 2 = ban UART. Ban 1 la BLE va KHONG tuong thich: goi lenh v1 dai 12 byte,
# v2 dai 13 (them crc8). Gateway kiem truong nay nen hai ben lech phien ban se bi
# tu choi thang thay vi doc lech truong roi ra lenh sai.
VERSION = 2

# Toc do UART. PHAI KHOP UNOQ_BAUD trong FirmWare/esp32-*/platformio.ini.
# 115200 la du: anh chup 39 byte moi 5 giay = 62 byte/giay. Khong nang cao hon —
# day di giua hai bo khong boc giap, toc do cao doi lay ti le loi bit chu khong
# doi lay gi ca o luu luong nay. Hai ben lech baud thi byte ra rac, va trieu
# chung giong het thieu day GND.
BAUD = 115200
MAX_ROOMS = 4

T_INVALID = -0x8000
H_INVALID = 0xFFFF
SILENCE_NEVER = 0xFFFF
CORNER_NONE = 0xFF

FLAG_WIFI_UP = 0x01
FLAG_MQTT_UP = 0x02
FLAG_OVERRIDE = 0x04
FLAG_OUT_ONLINE = 0x08

KIND_ADVICE = 0
KIND_COMMAND = 1


class Mode(IntEnum):
    """Khớp AcUnoQMode bên firmware VÀ app.models.enums.AcMode bên backend."""

    OFF = 0
    COOL = 1
    DRY = 2
    FAN = 3
    UNKNOWN = 0xFF

    @property
    def name_str(self) -> str:
        return "UNKNOWN" if self is Mode.UNKNOWN else self.name


# '<' = little-endian KHÔNG căn lề — bắt buộc, vì struct C bên kia là __packed__.
# Bỏ '<' thì Python tự chèn byte đệm và mọi trường sau ô đầu tiên lệch chỗ.
_SNAPSHOT_FMT = (
    "<"
    "BBBB"          # magic, version, room_count, flags
    "hHhH"          # t_in, h_in, t_out, h_out
    f"{MAX_ROOMS}h" # room_t
    f"{MAX_ROOMS}H" # room_h
    f"{MAX_ROOMS}B" # room_corner
    "H"             # cloud_silence_sec
    "Bb"            # ac_mode, ac_setpoint
    "H"             # uptime_min
    "B"             # crc8
)
SNAPSHOT_SIZE = struct.calcsize(_SNAPSHOT_FMT)

# magic, version, kind, mode, setpoint, reserved, seq, link_key, crc8
#
# CRC LA MOI O BAN UART. Ban BLE khong co vi tang lien ket cua Bluetooth da tu
# kiem CRC24 va loai goi hong truoc khi len tang ung dung. UART khong co gi nhu
# vay: day nhieu, cam rut giua chung, hai dau lech baud — tat ca deu day byte rac
# len thang. Voi anh chup thi mot bit lat lam sai mot so do trong mot nhip; voi
# LENH thi no doi setpoint roi di thang ra may lanh.
_COMMAND_FMT = "<BBBBbBHIB"
COMMAND_SIZE = struct.calcsize(_COMMAND_FMT)

# Chốt kích thước, khớp static_assert trong unoq-link-protocol.h. Thêm một trường
# ở một bên mà quên bên kia thì gói vẫn "giải mã thành công" — chỉ là mọi trường
# sau chỗ chèn đều lệch, và nhiệt độ phòng đọc ra thành số rác. CRC không cứu
# được: nó tính trên đúng số byte mà bên gửi nghĩ là đúng. Hai dòng này nổ ngay
# lúc import, tức là lúc dịch vụ khởi động, chứ không phải lúc đang lái máy lạnh.
assert SNAPSHOT_SIZE == 39, f"Snapshot {SNAPSHOT_SIZE} byte, firmware chốt 39"
assert COMMAND_SIZE == 13, f"Command {COMMAND_SIZE} byte, firmware chốt 13"


class ProtocolError(ValueError):
    """Gói không đúng khuôn — bên gọi phải BỎ, không được đoán."""


def crc8(data: bytes) -> int:
    """CRC8 Dallas/Maxim (đa thức phản chiếu 0x8C) — khớp acUnoQCrc8()."""
    crc = 0
    for byte in data:
        for _ in range(8):
            mix = (crc ^ byte) & 0x01
            crc >>= 1
            if mix:
                crc ^= 0x8C
            byte >>= 1
    return crc


def fnv1a(text: str) -> int:
    """FNV-1a 32-bit — khớp acUnoQLinkKey(). Dùng để băm ORG_ID thành link_key."""
    h = 2166136261
    for b in text.encode("utf-8"):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def _temp(raw: int) -> float | None:
    """None chứ không 0.0 cho "không có số đo": 0 °C là một nhiệt độ hợp lệ, và
    hiểu nó thành "thiếu" là cách nhanh nhất để một cảm biến hỏng kéo trung vị."""
    return None if raw == T_INVALID else raw / 100.0


def _rh(raw: int) -> float | None:
    return None if raw == H_INVALID else raw / 100.0


@dataclass(frozen=True)
class RoomSlot:
    corner: int | None  # nhãn góc node tự khai; None = ô trống
    temp: float | None
    humidity: float | None


@dataclass(frozen=True)
class Snapshot:
    """Ảnh chụp gateway gửi sang. Mọi số đo thiếu là None, không bao giờ là 0."""

    room_count: int
    flags: int
    t_in: float | None
    h_in: float | None
    t_out: float | None
    h_out: float | None
    rooms: list[RoomSlot]
    cloud_silence_sec: int | None  # None = máy chủ CHƯA TỪNG ra lệnh
    ac_mode: Mode
    ac_setpoint: int | None
    uptime_min: int

    @property
    def wifi_up(self) -> bool:
        return bool(self.flags & FLAG_WIFI_UP)

    @property
    def mqtt_up(self) -> bool:
        return bool(self.flags & FLAG_MQTT_UP)

    @property
    def override_active(self) -> bool:
        return bool(self.flags & FLAG_OVERRIDE)

    @property
    def outdoor_online(self) -> bool:
        return bool(self.flags & FLAG_OUT_ONLINE)


def parse_snapshot(data: bytes) -> Snapshot:
    """Giải mã một ảnh chụp. Ném ProtocolError nếu có bất cứ gì không khớp.

    Kiểm ĐỘ DÀI TRƯỚC TIÊN: trên UART, một khung có thể tới làm nhiều mảnh (đệm
    driver cắt bất kỳ đâu). Bên gọi phải gom đủ byte rồi mới đưa vào đây; dòng
    kiểm này biến "gọi sớm" thành một thông báo đọc được thay vì một IndexError.
    """
    if len(data) < SNAPSHOT_SIZE:
        raise ProtocolError(
            f"Ảnh chụp dài {len(data)} byte, cần {SNAPSHOT_SIZE}. "
            "Gần như chắc chắn MTU quá nhỏ nên gói bị cắt cụt."
        )
    fields = struct.unpack(_SNAPSHOT_FMT, data[:SNAPSHOT_SIZE])

    magic, version, room_count, flags = fields[0:4]
    if magic != MAGIC:
        raise ProtocolError(f"magic sai: {magic:#04x}")
    if version != VERSION:
        raise ProtocolError(
            f"phiên bản gói {version}, dịch vụ này hiểu {VERSION} — "
            "firmware gateway và edge-ai lệch nhau, nạp lại một bên."
        )
    if crc8(data[: SNAPSHOT_SIZE - 1]) != fields[-1]:
        raise ProtocolError("CRC sai")

    i = 4
    t_in, h_in, t_out, h_out = fields[i : i + 4]
    i += 4
    room_t = fields[i : i + MAX_ROOMS]
    i += MAX_ROOMS
    room_h = fields[i : i + MAX_ROOMS]
    i += MAX_ROOMS
    room_corner = fields[i : i + MAX_ROOMS]
    i += MAX_ROOMS
    silence, ac_mode, ac_setpoint, uptime_min = fields[i : i + 4]

    try:
        mode = Mode(ac_mode)
    except ValueError:
        mode = Mode.UNKNOWN

    return Snapshot(
        room_count=room_count,
        flags=flags,
        t_in=_temp(t_in),
        h_in=_rh(h_in),
        t_out=_temp(t_out),
        h_out=_rh(h_out),
        rooms=[
            RoomSlot(
                corner=None if room_corner[k] == CORNER_NONE else room_corner[k],
                temp=_temp(room_t[k]),
                humidity=_rh(room_h[k]),
            )
            for k in range(MAX_ROOMS)
        ],
        cloud_silence_sec=None if silence == SILENCE_NEVER else silence,
        ac_mode=mode,
        ac_setpoint=None if ac_setpoint < 0 else ac_setpoint,
        uptime_min=uptime_min,
    )


def build_command(*, kind: int, mode: Mode, setpoint: int | None, seq: int, link_key: int) -> bytes:
    """Đóng gói một đề xuất (kind=ADVICE) hoặc lệnh thật (kind=COMMAND).

    ``kind`` là ranh giới quan trọng nhất của cả giao thức: gateway CHỈ bắn hồng
    ngoại khi nhận COMMAND. Mặc định ở mọi lối gọi phải là ADVICE.
    """
    # Đóng gói với crc8 = 0 rồi tính CRC trên 12 byte đầu và ghi đè byte cuối.
    # Cùng cách firmware làm (acUnoQSealCommand) — và phải cùng cách, vì CRC tính
    # trên một chuỗi byte khác đi một byte là mọi lệnh bị gateway từ chối, mà
    # triệu chứng chỉ là "máy lạnh không nhúc nhích".
    body = struct.pack(
        _COMMAND_FMT,
        MAGIC,
        VERSION,
        kind,
        int(mode),
        -1 if setpoint is None else int(setpoint),
        0,                    # reserved
        seq & 0xFFFF,
        link_key & 0xFFFFFFFF,
        0,                    # crc8, điền ngay dưới
    )
    return body[:-1] + bytes([crc8(body[:-1])])
