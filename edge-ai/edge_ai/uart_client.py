"""The UART link to the gateway: read snapshots, send commands.

REPLACES ble_client.py. The reasoning for dropping Bluetooth is in
Firmware/shared/unoq-link-protocol.h; summarised by the measurement on real
hardware:

    Gateway, BLE on:    0.31 ESP-NOW packets/second
    Gateway, BLE off:   0.80 packets/second

Turning on Bluetooth FORCES the ESP32 to enable WiFi sleep (it aborts rather than
merely degrading), and with the radio asleep an ESP-NOW packet arriving at that
moment is lost. In other words, the BLE link ate ~60% of the receive capacity of the
very gateway it was serving.

LOSING THE CONNECTION IS ROUTINE, NOT EXCEPTIONAL: the gateway restarts, someone
unplugs the cable, the USB port gets renamed. So the lifecycle here is
open-read-reopen forever, and every error leads back to the top of the loop rather
than killing the service.
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
    """The gateway's serial port. Returns None if none is found.

    DO NOT PIN /dev/ttyUSB0: the number follows plug-in order, and adding any other
    USB-serial device pushes it to a different name. Time has already been lost to
    exactly this kind of thing with the board's IP address.
    """
    if configured:
        return configured

    ports = list(list_ports.comports())
    # Prefer the USB-UART bridge chips commonly used to connect an ESP32: CH34x,
    # CP210x, FTDI. If the board's UART pins are wired directly (with no bridge),
    # EDGE_UART_PORT has to be declared by hand -- Linux has no way to guess which
    # /dev/ttyS* is connected to what.
    for p in ports:
        blob = f"{p.description} {p.manufacturer or ''} {p.product or ''}".lower()
        if any(k in blob for k in ("ch340", "ch343", "cp210", "ftdi", "usb serial", "uart")):
            return p.device
    return ports[0].device if ports else None


class GatewayLink:
    """Holds one UART port to the gateway and reopens it whenever it breaks."""

    def __init__(self, settings) -> None:
        self._s = settings
        self._link_key = protocol.fnv1a(settings.org_id)
        self._serial: serial.Serial | None = None
        self._seq = 0

    @property
    def connected(self) -> bool:
        return self._serial is not None and self._serial.is_open

    async def run(self, on_snapshot: SnapshotHandler) -> None:
        """Lifecycle: open the port -> read -> forever. Never returns."""
        global _port_hint_shown
        while True:
            port = _find_port(getattr(self._s, "uart_port", None))
            if port is None:
                if not _port_hint_shown:
                    _port_hint_shown = True
                    logger.error(
                        "NO SERIAL PORT FOUND.\n"
                        "  Check: is the cable plugged in, and is the user running the\n"
                        "  service in the `dialout` group (`groups`)? Without the group the\n"
                        "  port DOES exist but opening it gives Permission denied -- easily\n"
                        "  misread as a missing cable.\n"
                        "  If you know the port name, declare it: EDGE_UART_PORT=/dev/ttyUSB0"
                    )
                await asyncio.sleep(self._s.reconnect_sec)
                continue

            try:
                await self._session(port, on_snapshot)
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # noqa: BLE001
                logger.warning("UART session dropped (%s): %s", port, exc)
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
        # timeout=0 (non-blocking) because this loop runs inside asyncio: a read that
        # blocks for 1 second stalls the whole service for 1 second, including when a
        # command needs sending.
        self._serial = serial.Serial(port, protocol.BAUD, timeout=0)
        logger.info("Opened %s @%d", port, protocol.BAUD)

        buf = bytearray()
        while True:
            chunk = self._serial.read(256)
            if chunk:
                buf.extend(chunk)
                self._consume(buf, on_snapshot)
            else:
                # A short sleep rather than a blocking read: keeps the asyncio loop
                # breathing.
                await asyncio.sleep(0.05)

    def _consume(self, buf: bytearray, on_snapshot: SnapshotHandler) -> None:
        """Extract every complete snapshot in the buffer.

        FRAME SYNC VIA THE MAGIC BYTE: packets are a fixed 39 bytes, starting with
        the magic and ending with a CRC. On a mismatch, slide ONE byte and resync --
        do not clear the buffer, because the real magic byte may be inside the bytes
        just collected (half an old packet stuck to half a new one), and clearing
        would also discard a good frame sitting immediately after a corrupt one.
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
                del buf[:1]        # slide one byte and keep looking for the magic
                continue
            del buf[: protocol.SNAPSHOT_SIZE]
            on_snapshot(snap)

    async def send(self, *, kind: int, mode: protocol.Mode, setpoint: int | None) -> bool:
        """Send an advice or a command. Returns False if the port is not open yet."""
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
            logger.warning("Could not send the command: %s", exc)
            return False
