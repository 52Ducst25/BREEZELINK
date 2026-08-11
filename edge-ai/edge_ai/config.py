"""Configuration for the edge-AI service, read from the environment.

Nothing here has a secret as its default. The service refuses to start rather
than fall back to a guess for ``EDGE_ORG_ID`` — that value is hashed into the
``link_key`` the gateway checks, so a wrong one means every command this node
sends is silently rejected, with the gateway logging "UNO Q của hộ khác?" and
this node logging nothing at all.
"""

import os
from dataclasses import dataclass


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


@dataclass(frozen=True)
class Settings:
    org_id: str

    # Địa chỉ BLE của gateway. ĐỂ TRỐNG là đúng trong hầu hết trường hợp: dịch vụ
    # tự quét theo UUID DỊCH VỤ, nên thay bo gateway không phải sửa cấu hình.
    # Chỉ điền khi trong tầm sóng có hai gateway (lắp thử hai hộ cạnh nhau).
    gateway_address: str | None
    scan_timeout_sec: float
    reconnect_sec: float

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

    # Chỉ đề xuất, không bao giờ ra lệnh — kể cả khi mất cloud. Bật khi chạy thử
    # tại nhà khách để đối chiếu quyết định edge với quyết định cloud trước khi
    # giao cho nó quyền thật.
    advisory_only: bool


def load() -> Settings:
    """Đọc cấu hình từ môi trường (đã nạp .env nếu có)."""
    return Settings(
        org_id=_require("EDGE_ORG_ID"),
        gateway_address=os.getenv("EDGE_GATEWAY_ADDRESS", "").strip() or None,
        scan_timeout_sec=_num("EDGE_SCAN_TIMEOUT_SEC", 20),
        reconnect_sec=_num("EDGE_RECONNECT_SEC", 5),
        history_sec=_num("EDGE_HISTORY_SEC", 1800),
        takeover_after_sec=_num("EDGE_TAKEOVER_AFTER_SEC", 300),
        tick_sec=_num("EDGE_TICK_SEC", 30),
        advisory_only=os.getenv("EDGE_ADVISORY_ONLY", "").strip().lower()
        in {"1", "true", "yes"},
    )
