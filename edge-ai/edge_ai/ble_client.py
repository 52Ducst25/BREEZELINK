"""Đường Bluetooth tới gateway: quét, kết nối, nghe ảnh chụp, gửi lệnh.

UNO Q đóng vai CENTRAL (GATT client) — xem FirmWare/shared/unoq-link-protocol.h
cho lý do chia vai.

VÌ SAO BLUETOOTH CHỨ KHÔNG PHẢI MQTT NHƯ BẢN ĐẦU: lớp dự phòng phải sống sót
đúng cái sự cố mà nó sinh ra để chịu đựng. Nói chuyện với gateway qua broker
nghĩa là khi mất mạng — đúng lúc cần nó nhất — dịch vụ này cũng mất luôn đường
tới gateway và không cứu được gì. BLE là liên kết trực tiếp giữa hai thiết bị
đặt cùng phòng, không đi qua router, không đi qua internet.

KẾT NỐI LẠI LÀ CHUYỆN THƯỜNG, KHÔNG PHẢI NGOẠI LỆ: gateway khởi động lại, người
ta rút điện, sóng chập. Nên vòng đời ở đây là một vòng lặp quét-kết-nối-nghe
chạy mãi, và mọi lỗi đều dẫn về đầu vòng thay vì làm chết dịch vụ.
"""

import asyncio
import logging
from collections.abc import Callable

from bleak import BleakClient, BleakScanner

from edge_ai import protocol
from edge_ai.protocol import ProtocolError, Snapshot

logger = logging.getLogger("edge.ble")

SnapshotHandler = Callable[[Snapshot], None]


class GatewayLink:
    """Giữ một kết nối tới gateway và tự dựng lại khi đứt."""

    def __init__(self, settings) -> None:
        self._s = settings
        self._link_key = protocol.fnv1a(settings.org_id)
        self._client: BleakClient | None = None
        self._seq = 0
        self._connected = asyncio.Event()

    @property
    def connected(self) -> bool:
        return self._client is not None and self._client.is_connected

    async def _find(self) -> str | None:
        """Địa chỉ gateway, tìm theo SERVICE UUID chứ không theo tên.

        Tên thiết bị đổi được và trùng được; UUID dịch vụ thì không. Tìm theo tên
        là cách chắc chắn nhất để một hôm nào đó kết nối nhầm sang gateway của
        hàng xóm trong chung cư — và `link_key` sẽ chặn lệnh, nhưng chỉ SAU khi
        đã ngồi nghe nhầm số đo của nhà người ta.
        """
        if self._s.gateway_address:
            return self._s.gateway_address
        logger.info("Đang quét gateway (service %s)…", protocol.SERVICE_UUID)
        device = await BleakScanner.find_device_by_filter(
            lambda _d, adv: protocol.SERVICE_UUID.lower()
            in [u.lower() for u in adv.service_uuids],
            timeout=self._s.scan_timeout_sec,
        )
        return device.address if device else None

    async def run(self, on_snapshot: SnapshotHandler) -> None:
        """Vòng đời: quét -> kết nối -> nghe, mãi mãi. Không bao giờ trả về."""
        while True:
            try:
                await self._session(on_snapshot)
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # noqa: BLE001
                # Ngăn xếp BLE ném đủ loại lỗi (thiết bị biến mất giữa chừng,
                # D-Bus timeout, adapter bị reset). Không cái nào đáng làm chết
                # dịch vụ — tất cả đều nghĩa là "thử lại".
                logger.warning("Phiên BLE đứt: %s", exc)
            self._client = None
            self._connected.clear()
            await asyncio.sleep(self._s.reconnect_sec)

    async def _session(self, on_snapshot: SnapshotHandler) -> None:
        address = await self._find()
        if address is None:
            logger.warning("Không thấy gateway trong tầm BLE — thử lại")
            return

        def _on_notify(_sender, data: bytearray) -> None:
            try:
                snapshot = protocol.parse_snapshot(bytes(data))
            except ProtocolError as exc:
                # Một gói hỏng không được làm chết kết nối: gói sau vẫn có thể
                # tốt, và mất kết nối vì một gói lẻ là đổi một lỗi nhỏ lấy một
                # khoảng mù dài.
                logger.warning("Bỏ ảnh chụp không hợp lệ: %s", exc)
                return
            on_snapshot(snapshot)

        async with BleakClient(address) as client:
            self._client = client
            logger.info("Đã nối gateway %s (MTU %s)", address, getattr(client, "mtu_size", "?"))

            mtu = getattr(client, "mtu_size", 0) or 0
            if mtu and mtu - 3 < protocol.SNAPSHOT_SIZE:
                # Kêu to vì hỏng CÂM: notify vượt MTU bị cắt cụt và parse sẽ báo
                # "cần N byte" mỗi 5 giây mà không ai hiểu vì sao.
                logger.error(
                    "MTU %d quá nhỏ cho ảnh chụp %d byte — gói sẽ bị cắt cụt. "
                    "Kiểm tra BlueZ có thương lượng MTU không.",
                    mtu, protocol.SNAPSHOT_SIZE,
                )

            # ĐỌC MỘT PHÁT NGAY khi vừa nối, đừng chỉ chờ notify: nhịp notify của
            # gateway là 5 giây, và không đọc trước thì vòng điều khiển đầu tiên
            # chạy trong mù. READ còn đi được nhiều lượt ATT nên nó đủ dữ liệu
            # ngay cả khi MTU hụt.
            try:
                _on_notify(None, await client.read_gatt_char(protocol.SNAPSHOT_UUID))
            except Exception as exc:  # noqa: BLE001
                logger.debug("Đọc ảnh chụp đầu tiên không được: %s", exc)

            await client.start_notify(protocol.SNAPSHOT_UUID, _on_notify)
            self._connected.set()

            # Giữ phiên sống. bleak báo mất kết nối bằng cách cho is_connected
            # thành False; không có sự kiện nào phải chờ nên chỉ cần thăm định kỳ.
            while client.is_connected:
                await asyncio.sleep(1.0)

        logger.info("Gateway ngắt kết nối")

    async def send(self, *, kind: int, mode: protocol.Mode, setpoint: int | None) -> bool:
        """Gửi một đề xuất hoặc lệnh. Trả False nếu chưa nối được gateway."""
        if not self.connected or self._client is None:
            return False
        self._seq = (self._seq + 1) & 0xFFFF
        payload = protocol.build_command(
            kind=kind, mode=mode, setpoint=setpoint,
            seq=self._seq, link_key=self._link_key,
        )
        # response=True: một lệnh quyết định máy nén chạy hay không thì bên gửi
        # phải biết nó đã tới nơi.
        await self._client.write_gatt_char(protocol.COMMAND_UUID, payload, response=True)
        return True
