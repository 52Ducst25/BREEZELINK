"""Đường UART tới gateway: đọc ảnh chụp, gửi lệnh.

THAY CHO ble_client.py. Lý do bỏ Bluetooth nằm ở FirmWare/shared/unoq-link-protocol.h;
tóm tắt bằng số đo trên bo thật:

    Gateway, BLE bật:   0,31 gói ESP-NOW/giây
    Gateway, BLE tắt:   0,80 gói/giây

Bật Bluetooth thì ESP32 BẮT BUỘC phải bật ngủ WiFi (nó abort chứ không chạy kém
đi), mà radio ngủ thì gói ESP-NOW đến đúng lúc đó là mất. Tức là đường BLE ăn mất
~60% khả năng thu của chính cái gateway nó phục vụ.

MẤT KẾT NỐI LÀ CHUYỆN THƯỜNG, KHÔNG PHẢI NGOẠI LỆ: gateway khởi động lại, ai đó
rút dây, cổng USB đổi tên. Nên vòng đời ở đây là mở-đọc-lại mãi mãi, và mọi lỗi
đều dẫn về đầu vòng thay vì làm chết dịch vụ.
"""

import asyncio
import logging
from collections.abc import Callable

import serial
from serial.tools import list_ports

from edge_ai import protocol
from edge_ai.protocol import ProtocolError, Snapshot

logger = logging.getLogger("edge.uart")

SnapshotHandler = Callable[[Snapshot], None]

_port_hint_shown = False


def _find_port(configured: str | None) -> str | None:
    """Cổng serial của gateway. Trả None nếu không tìm thấy.

    KHÔNG GHIM /dev/ttyUSB0: số thứ tự đổi theo thứ tự cắm, và cắm thêm bất kỳ
    thiết bị USB-serial nào cũng đẩy nó sang tên khác. Đã mất thời gian vì đúng
    loại chuyện này với địa chỉ IP của bo.
    """
    if configured:
        return configured

    ports = list(list_ports.comports())
    # Ưu tiên các chip cầu USB-UART hay dùng để nối ESP32: CH34x, CP210x, FTDI.
    # Nếu nối thẳng chân UART của bo (không qua cầu) thì phải khai EDGE_UART_PORT
    # tay — Linux không có cách nào đoán được /dev/ttyS* nào đang nối vào đâu.
    for p in ports:
        blob = f"{p.description} {p.manufacturer or ''} {p.product or ''}".lower()
        if any(k in blob for k in ("ch340", "ch343", "cp210", "ftdi", "usb serial", "uart")):
            return p.device
    return ports[0].device if ports else None


class GatewayLink:
    """Giữ một cổng UART tới gateway và tự mở lại khi đứt."""

    def __init__(self, settings) -> None:
        self._s = settings
        self._link_key = protocol.fnv1a(settings.org_id)
        self._serial: serial.Serial | None = None
        self._seq = 0

    @property
    def connected(self) -> bool:
        return self._serial is not None and self._serial.is_open

    async def run(self, on_snapshot: SnapshotHandler) -> None:
        """Vòng đời: mở cổng -> đọc -> mãi mãi. Không bao giờ trả về."""
        global _port_hint_shown
        while True:
            port = _find_port(getattr(self._s, "uart_port", None))
            if port is None:
                if not _port_hint_shown:
                    _port_hint_shown = True
                    logger.error(
                        "KHÔNG THẤY CỔNG SERIAL NÀO.\n"
                        "  Kiểm: dây đã cắm chưa, và user chạy dịch vụ có trong nhóm\n"
                        "  `dialout` không (`groups`). Thiếu nhóm thì cổng CÓ tồn tại\n"
                        "  nhưng mở ra bị Permission denied — dễ đọc nhầm thành mất dây.\n"
                        "  Biết tên cổng thì khai thẳng: EDGE_UART_PORT=/dev/ttyUSB0"
                    )
                await asyncio.sleep(self._s.reconnect_sec)
                continue

            try:
                await self._session(port, on_snapshot)
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # noqa: BLE001
                logger.warning("Phiên UART đứt (%s): %s", port, exc)
            finally:
                self._close()
            await asyncio.sleep(self._s.reconnect_sec)

    def _close(self) -> None:
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:  # noqa: BLE001
                pass
            self._serial = None

    async def _session(self, port: str, on_snapshot: SnapshotHandler) -> None:
        # timeout=0 (không chặn) vì vòng lặp này chạy trong asyncio: một lần đọc
        # chặn 1 giây là cả dịch vụ đứng im 1 giây, kể cả lúc cần gửi lệnh.
        self._serial = serial.Serial(port, protocol.BAUD, timeout=0)
        logger.info("Đã mở %s @%d", port, protocol.BAUD)

        buf = bytearray()
        while True:
            chunk = self._serial.read(256)
            if chunk:
                buf.extend(chunk)
                self._consume(buf, on_snapshot)
            else:
                # Ngủ ngắn thay vì đọc chặn: giữ vòng lặp asyncio thở được.
                await asyncio.sleep(0.05)

    def _consume(self, buf: bytearray, on_snapshot: SnapshotHandler) -> None:
        """Bóc mọi ảnh chụp đầy đủ trong đệm.

        ĐỒNG BỘ KHUNG BẰNG MAGIC: gói cố định 39 byte, mở đầu bằng magic, kết
        bằng CRC. Sai thì trượt MỘT byte rồi tìm lại — không xoá sạch đệm, vì
        magic thật có thể nằm ngay trong đám byte vừa gom (nửa gói cũ dính nửa
        gói mới), và xoá sạch là mất luôn khung tốt đứng ngay sau khung hỏng.
        """
        while True:
            start = buf.find(bytes([protocol.MAGIC]))
            if start < 0:
                buf.clear()
                return
            if start > 0:
                del buf[:start]
            if len(buf) < protocol.SNAPSHOT_SIZE:
                return

            frame = bytes(buf[: protocol.SNAPSHOT_SIZE])
            try:
                snap = protocol.parse_snapshot(frame)
            except ProtocolError:
                del buf[:1]        # trượt một byte, tìm magic tiếp
                continue
            del buf[: protocol.SNAPSHOT_SIZE]
            on_snapshot(snap)

    async def send(self, *, kind: int, mode: protocol.Mode, setpoint: int | None) -> bool:
        """Gửi một đề xuất hoặc lệnh. Trả False nếu chưa mở được cổng."""
        if not self.connected or self._serial is None:
            return False
        self._seq = (self._seq + 1) & 0xFFFF
        frame = protocol.build_command(
            kind=kind, mode=mode, setpoint=setpoint, seq=self._seq, link_key=self._link_key
        )
        try:
            self._serial.write(frame)
            return True
        except Exception as exc:  # noqa: BLE001
            logger.warning("Gửi lệnh không được: %s", exc)
            return False
