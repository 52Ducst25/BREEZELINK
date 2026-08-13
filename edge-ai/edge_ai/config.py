"""Configuration for the edge-AI service, read from the environment.

Nothing here has a secret as its default. The service refuses to start rather
than fall back to a guess for ``EDGE_ORG_ID`` — that value is hashed into the
``link_key`` the gateway checks, so a wrong one means every command this node
sends is silently rejected, with the gateway logging "UNO Q của hộ khác?" and
this node logging nothing at all.
"""

import os
from dataclasses import dataclass
from pathlib import Path


class ConfigError(RuntimeError):
    """Raised at startup for a missing or nonsensical setting."""


def _require(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise ConfigError(f"Thiếu biến môi trường bắt buộc: {name}")
    return value


def _num(name: str, default: float) -> float:
    raw = os.getenv(name, "").strip()
    if not raw:
        return default
    try:
        return float(raw)
    except ValueError as exc:
        raise ConfigError(f"{name} phải là số, đang là {raw!r}") from exc


def _coords() -> tuple[float | None, float | None]:
    """Toạ độ cho dự báo. Phải có ĐỦ CẢ HAI hoặc không có gì.

    Chỉ khai một trong hai là lỗi cấu hình chứ không phải "tắt": nó gần như chắc
    chắn là gõ sót, và im lặng bỏ qua sẽ để lại một dự báo không bao giờ chạy mà
    không ai hiểu vì sao.
    """
    lat_raw = os.getenv("EDGE_LAT", "").strip()
    lon_raw = os.getenv("EDGE_LON", "").strip()
    if not lat_raw and not lon_raw:
        return None, None
    if not lat_raw or not lon_raw:
        raise ConfigError("EDGE_LAT và EDGE_LON phải khai cùng nhau, hoặc bỏ trống cả hai")
    try:
        lat, lon = float(lat_raw), float(lon_raw)
    except ValueError as exc:
        raise ConfigError(f"EDGE_LAT/EDGE_LON phải là số: {lat_raw!r}, {lon_raw!r}") from exc
    if not (-90 <= lat <= 90 and -180 <= lon <= 180):
        raise ConfigError(f"Toạ độ ngoài dải hợp lệ: {lat}, {lon}")
    return lat, lon


def _ir_temps() -> tuple[int, ...]:
    """Đọc EDGE_IR_TEMPS="24,25,26,27,28". Rỗng = tự học bằng quan sát.

    Từ chối số vô lý thay vì lọc im lặng: một mức 250 lọt vào đây sẽ được
    ``nearest_captured_temp`` coi là ứng viên hợp lệ, và gateway thì không có mã
    cho nó nên lệnh bay ra rồi rơi vào hư không — triệu chứng duy nhất là "máy
    lạnh không nhúc nhích".
    """
    raw = os.getenv("EDGE_IR_TEMPS", "").strip()
    if not raw:
        return ()
    out: list[int] = []
    for part in raw.replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        try:
            value = int(part)
        except ValueError as exc:
            raise ConfigError(f"EDGE_IR_TEMPS có phần tử không phải số: {part!r}") from exc
        if not 5 <= value <= 35:
            raise ConfigError(f"EDGE_IR_TEMPS: {value}°C nằm ngoài dải máy lạnh dân dụng")
        out.append(value)
    return tuple(sorted(set(out)))


def _link() -> str:
    """Chọn đường tới gateway. "auto" = dò xem có đang ở trong App Lab không.

    Dấu hiệu nhận biết là CÁI SOCKET, không phải gói `arduino` import được: gói
    đó nằm sẵn trong ảnh nền nên nó có mặt kể cả khi router chưa chạy, và lúc đó
    `Bridge.provide()` sẽ ném ra một lỗi kết nối khó đọc thay vì rơi về pyserial.
    """
    choice = os.getenv("EDGE_LINK", "auto").strip().lower() or "auto"
    if choice in {"bridge", "serial"}:
        return choice
    if choice != "auto":
        raise ConfigError(f"EDGE_LINK phải là auto|bridge|serial, đang là {choice!r}")
    return "bridge" if os.path.exists("/var/run/arduino-router.sock") else "serial"


@dataclass(frozen=True)
class Settings:
    org_id: str

    # Địa chỉ BLE của gateway. ĐỂ TRỐNG là đúng trong hầu hết trường hợp: dịch vụ
    # tự quét theo UUID DỊCH VỤ, nên thay bo gateway không phải sửa cấu hình.
    # Chỉ điền khi trong tầm sóng có hai gateway (lắp thử hai hộ cạnh nhau).
    # None = tu do (uu tien chip cau CH34x/CP210x/FTDI). Khai tay khi noi thang
    # chan UART: Linux khong doan duoc /dev/ttyS* nao dang noi vao dau.
    uart_port: str | None
    scan_timeout_sec: float
    reconnect_sec: float

    # Đường tới gateway: "bridge" (qua sketch trên STM32) hay "serial" (cổng
    # USB-TTL của nửa Linux). "auto" chọn bridge nếu đang chạy trong App Lab.
    #
    # HAI ĐƯỜNG NÀY KHÔNG THỂ CÙNG ĐÚNG: dây chỉ cắm được vào một chỗ. Cắm vào
    # D0/D1 thì nửa Linux KHÔNG có /dev/tty* nào tương ứng — pyserial sẽ vớ đại
    # một cổng khác rồi ngồi chờ mãi mãi, và triệu chứng ("không thấy ảnh chụp")
    # giống hệt đứt dây. Nên mặc định là dò, không phải đoán.
    link: str

    # Cửa sổ trượt giữ số đo từng góc (giây). Đủ dài để hồi quy tuyến tính có gì
    # mà bám, đủ ngắn để một chiều nắng gắt không bị pha loãng bởi buổi sáng mát.
    history_sec: float

    # Im lặng bao lâu từ phía máy chủ thì UNO Q giành quyền lái.
    #
    # Con số này so với `cloud_silence_sec` GATEWAY BÁO SANG, không phải một phép
    # đo của riêng dịch vụ này — gateway giữ phiên MQTT nên nó biết chắc chắn hơn.
    #
    # 300s rộng có chủ đích: hai bên cùng ra lệnh là máy lạnh nhận hai lệnh trái
    # nhau trong cùng một phút, và triệu chứng (nhiệt độ tự nhảy) trông y hệt lỗi
    # thuật toán. Thà giành muộn.
    takeover_after_sec: float

    # Nhịp vòng điều khiển. Không cần dày: nhiệt độ phòng không đổi trong 30
    # giây, và mỗi vòng đều có thể sinh ra một lệnh IR.
    tick_sec: float

    # Những mức nhiệt độ hộ này ĐÃ HỌC MÃ IR (chế độ COOL).
    #
    # PHẢI KHAI TAY vì node edge không có đường tới Postgres — bảng `ir_codes`
    # nằm trên máy chủ, mà cả lý do tồn tại của node này là chạy tiếp khi mất
    # máy chủ. Để trống thì nó rơi về cách cũ: tự học bằng cách quan sát máy chủ
    # ra lệnh, và cách đó có một cái bẫy:
    #
    #   Vừa khởi động, mới thấy đúng một mức (vd 27). nearest_captured_temp()
    #   luôn trả về mức GẦN NHẤT trong danh sách nó có — nên mục tiêu 24.5 bị
    #   ép thành 27, không lỗi, không cảnh báo. Danh sách lấp một nửa nguy hiểm
    #   hơn danh sách rỗng: rỗng thì NoIrCodesError chặn lại.
    #
    # Danh sách thật lấy bằng:
    #   SELECT DISTINCT temp FROM ir_codes WHERE mode='COOL' ORDER BY temp;
    ir_temps: tuple[int, ...]

    # Lịch sử cục bộ (SQLite). Đây là nền cho mô hình nhiệt: không có nó thì mỗi
    # lần khởi động lại dịch vụ là mô hình quay về con số 0 và mất vài giờ mới
    # dùng được. Để trống = cạnh gói edge_ai.
    history_db: str
    history_days: float

    # Toạ độ để lấy dự báo nhiệt độ ngoài trời (open-meteo, không cần khoá API).
    # Cả hai cùng None = tắt dự báo; lúc đó mô hình vẫn chạy nhưng khi nhìn xa
    # hơn ~15 phút nó phải giả định trời ngoài đứng yên.
    lat: float | None
    lon: float | None

    # Chỉ đề xuất, không bao giờ ra lệnh — kể cả khi mất cloud. Bật khi chạy thử
    # tại nhà khách để đối chiếu quyết định edge với quyết định cloud trước khi
    # giao cho nó quyền thật.
    advisory_only: bool


def _default_db() -> str:
    """Thư mục ``data/`` cạnh gói edge_ai.

    PHẢI NẰM TRONG THƯ MỤC APP, không có lựa chọn khác: container của App Lab chỉ
    gắn đúng một chỗ vừa ghi được vừa sống sót qua lần dựng lại — chính thư mục
    app (``/app``). Mọi đường dẫn khác đều biến mất cùng container.

    RIÊNG MỘT THƯ MỤC CON, không rải thẳng cạnh mã nguồn. SQLite tạo BA file
    (``.db``, ``.db-wal``, ``.db-shm``), cộng bộ đệm dự báo là bốn. Để chúng ngang
    hàng với ``main.py`` thì trình duyệt tệp của App Lab liệt kê lẫn vào mã nguồn,
    và bấm nhầm vào một file nhị phân sẽ mở nó trong trình soạn thảo văn bản —
    lưu lại một lần là hỏng cả cơ sở dữ liệu.
    """
    return str(Path(__file__).resolve().parent.parent / "data" / "breezelink-history.db")


def load() -> Settings:
    """Đọc cấu hình từ môi trường (đã nạp .env nếu có)."""
    lat, lon = _coords()
    return Settings(
        org_id=_require("EDGE_ORG_ID"),
        uart_port=os.getenv("EDGE_UART_PORT", "").strip() or None,
        link=_link(),
        ir_temps=_ir_temps(),
        history_db=os.getenv("EDGE_HISTORY_DB", "").strip() or _default_db(),
        history_days=_num("EDGE_HISTORY_DAYS", 30),
        lat=lat,
        lon=lon,
        scan_timeout_sec=_num("EDGE_SCAN_TIMEOUT_SEC", 20),
        reconnect_sec=_num("EDGE_RECONNECT_SEC", 5),
        history_sec=_num("EDGE_HISTORY_SEC", 1800),
        takeover_after_sec=_num("EDGE_TAKEOVER_AFTER_SEC", 300),
        tick_sec=_num("EDGE_TICK_SEC", 30),
        advisory_only=os.getenv("EDGE_ADVISORY_ONLY", "").strip().lower()
        in {"1", "true", "yes"},
    )
