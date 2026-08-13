"""Đường tới gateway đi QUA STM32, không qua cổng USB của Linux.

THAY CHO uart_client.py KHI CHẠY TRONG ARDUINO APP LAB. Hai module cùng một giao
diện (``run`` / ``send`` / ``connected``) nên ``Controller`` không biết mình đang
nói chuyện qua đường nào — chọn ở ``main.py``.

VÌ SAO PHẢI CÓ BẢN NÀY: dây từ ESP32-S3 cắm vào D0/D1, tức là USART1 của con
STM32U585, KHÔNG phải một cổng USB-serial của nửa Linux. Nửa Linux không nhìn
thấy chân đó — không có /dev/tty* nào tương ứng, nên pyserial không có gì để mở.
Người trung gian là ``arduino-router``: sketch nói RPC với nó qua /dev/ttyHS1,
Python nói RPC với nó qua /var/run/arduino-router.sock.

    ESP32-S3 ──UART D0/D1──► STM32 (sketch) ──RPC──► router ──sock──► Python

CHỌN CÁCH NÀY THAY VÌ NỐI USB-TTL VÀO NỬA LINUX: bớt một sợi dây và một con chip
cầu, và quan trọng hơn là mọi thứ hiện trong App Lab — bấm Run là thấy cả log
sketch lẫn log Python trong cùng một chỗ.

CHỞ HEX CHỨ KHÔNG CHỞ TỪNG TRƯỜNG. Sketch không giải mã gói: nó kiểm khung
(magic + version + CRC) rồi đẩy nguyên 39 byte sang đây dưới dạng 78 ký tự hex.
Nhờ vậy bố cục gói chỉ nằm ở HAI chỗ đã chốt sẵn với nhau — struct C và
``protocol.py`` — chứ không phát sinh chỗ thứ ba trong sketch phải nhớ sửa theo.
Chiều ngược lại cũng vậy: ``protocol.build_command()`` đóng gói và ký CRC ở đây,
sketch chỉ giải hex rồi ghi thẳng ra dây.

  Hệ quả tốt kèm theo: ORG_ID không cần có mặt trong sketch. ``link_key`` băm từ
  nó được tính ở Python, nên thư mục app đem đi đâu cũng không mang theo định
  danh của hộ nào.
"""

import asyncio
import logging
import time
from collections.abc import Callable

from edge_ai import protocol
from edge_ai.protocol import ProtocolError, Snapshot

logger = logging.getLogger("edge.bridge")

SnapshotHandler = Callable[[Snapshot], None]

# Tên phương thức RPC. PHẢI KHỚP sketch — sai tên thì router định tuyến vào hư
# không và KHÔNG BÁO LỖI: bên gửi notify là fire-and-forget, bên nhận chỉ đơn
# giản không bao giờ được gọi. Triệu chứng y hệt đứt dây.
RPC_SNAPSHOT = "gw/snapshot"
RPC_COMMAND = "gw/command"

# Gateway đẩy ảnh chụp mỗi 5 giây. 30 giây = lỡ sáu nhịp liền mới coi là đứt —
# đủ rộng để một lần biên dịch lại sketch (STM32 khởi động lại) không bị đọc
# thành mất kết nối, đủ hẹp để không ra lệnh dựa trên số đo cũ cả phút.
STALE_AFTER_SEC = 30.0


class GatewayLink:
    """Nói chuyện với gateway qua RouterBridge. Cùng giao diện với bản pyserial."""

    def __init__(self, settings) -> None:
        self._s = settings
        self._link_key = protocol.fnv1a(settings.org_id)
        self._seq = 0
        self._last_rx = 0.0
        self._bridge = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._on_snapshot: SnapshotHandler | None = None
        self._bad = 0

    @property
    def connected(self) -> bool:
        """Có nghe thấy gateway gần đây không.

        KHÔNG hỏi trạng thái socket: socket tới router luôn mở kể cả khi ESP32-S3
        đã rút dây, nên nó trả lời "có kết nối" cho một đường đã chết. Thứ duy
        nhất chứng minh cả chuỗi còn sống là một khung hợp lệ vừa tới.
        """
        return self._last_rx > 0.0 and (time.monotonic() - self._last_rx) < STALE_AFTER_SEC

    async def run(self, on_snapshot: SnapshotHandler) -> None:
        """Đăng ký nơi nhận rồi đỗ ở đây. Không bao giờ trả về.

        Khác bản pyserial ở chỗ KHÔNG có vòng mở-lại: router tự lo việc kết nối
        lại, và ``provide`` giữ nguyên đăng ký qua các lần sketch khởi động lại.
        Vòng lặp dưới đây chỉ để báo cáo, không để sửa chữa gì.
        """
        self._loop = asyncio.get_running_loop()
        self._on_snapshot = on_snapshot

        from arduino.app_utils import Bridge  # chỉ có trong container của App Lab

        self._bridge = Bridge
        Bridge.provide(RPC_SNAPSHOT, self._on_frame)
        logger.info("Đã đăng ký %s — chờ sketch đẩy ảnh chụp sang", RPC_SNAPSHOT)

        warned = False
        while True:
            await asyncio.sleep(STALE_AFTER_SEC)
            if self.connected:
                warned = False
                continue
            if warned:
                continue
            warned = True
            logger.warning(
                "Không có ảnh chụp nào trong %.0fs. Kiểm theo thứ tự này:\n"
                "  1. Tab Sketch trong App Lab có dòng [rx] không? Không có nghĩa là\n"
                "     UART chết — kiểm ESP32-S3 GPIO18 -> D0, GPIO17 -> D1, và GND chung.\n"
                "  2. Có [rx] mà không có dòng này nghĩa là RPC chết — tên phương thức\n"
                "     hai bên phải cùng là %r.",
                STALE_AFTER_SEC, RPC_SNAPSHOT,
            )

    # -- nhận ------------------------------------------------------------------

    def _on_frame(self, hex_frame: str) -> None:
        """Sketch gọi hàm này. CHẠY TRÊN LUỒNG CỦA BRIDGE, không phải luồng asyncio.

        Nên nó không được đụng vào bất cứ trạng thái nào của ``Controller`` —
        ``RoomStore`` và ``tick()`` đều không có khoá, và sửa cửa sổ lịch sử giữa
        lúc vòng điều khiển đang duyệt nó là loại lỗi chỉ hiện ra lúc chạy lâu.
        ``call_soon_threadsafe`` đẩy việc về đúng luồng rồi mới gọi.
        """
        try:
            raw = bytes.fromhex(hex_frame)
        except (ValueError, TypeError):
            self._note_bad("hex hỏng")
            return

        try:
            snap = protocol.parse_snapshot(raw)
        except ProtocolError as exc:
            self._note_bad(str(exc))
            return

        self._last_rx = time.monotonic()
        self._bad = 0
        if self._loop is not None and self._on_snapshot is not None:
            self._loop.call_soon_threadsafe(self._on_snapshot, snap)

    def _note_bad(self, why: str) -> None:
        """Sketch đã kiểm CRC rồi, nên hỏng ở đây là hỏng SAU khi qua RPC.

        Chỉ kêu ở lần đầu của mỗi chuỗi: một gói lệch phiên bản sẽ lặp lại mỗi 5
        giây mãi mãi, và log kín đặc một dòng lặp là cách chắc chắn nhất để không
        ai đọc nó nữa.
        """
        self._bad += 1
        if self._bad == 1:
            logger.warning("Gói qua được sketch nhưng hỏng ở đây: %s", why)

    # -- gửi -------------------------------------------------------------------

    async def send(self, *, kind: int, mode: protocol.Mode, setpoint: int | None) -> bool:
        """Gửi đề xuất hoặc lệnh. Trả False nếu chuỗi đang đứt ở đâu đó."""
        if self._bridge is None or not self.connected:
            return False

        self._seq = (self._seq + 1) & 0xFFFF
        frame = protocol.build_command(
            kind=kind, mode=mode, setpoint=setpoint, seq=self._seq, link_key=self._link_key
        )
        try:
            # notify chứ không call: sketch ghi ra dây rồi thôi, không có gì để
            # trả lời, và một `call` sẽ chặn vòng điều khiển 10 giây mỗi lần
            # sketch bận đọc UART.
            self._bridge.notify(RPC_COMMAND, frame.hex())
            return True
        except Exception as exc:  # noqa: BLE001
            logger.warning("Gửi lệnh qua bridge không được: %s", exc)
            return False
