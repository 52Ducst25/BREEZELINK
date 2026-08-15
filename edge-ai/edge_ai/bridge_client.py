"""The link to the gateway THROUGH the STM32, not through a Linux USB port.

REPLACES uart_client.py WHEN RUNNING INSIDE ARDUINO APP LAB. Both modules share one
interface (``run`` / ``send`` / ``connected``) so ``Controller`` has no idea which
path it is talking over -- the choice is made in ``main.py``.

WHY THIS VERSION IS NEEDED: the cable from the ESP32-S3 goes into D0/D1, i.e. USART1
of the STM32U585, NOT a USB-serial port on the Linux half. The Linux half cannot see
those pins -- there is no corresponding /dev/tty*, so pyserial has nothing to open.
The intermediary is ``arduino-router``: the sketch speaks RPC to it over /dev/ttyHS1,
and Python speaks RPC to it over /var/run/arduino-router.sock.

    ESP32-S3 ──UART D0/D1──► STM32 (sketch) ──RPC──► router ──sock──► Python

CHOSEN OVER WIRING A USB-TTL INTO THE LINUX HALF: one less cable and one less bridge
chip, and more importantly everything shows up inside App Lab -- press Run and you see
both the sketch log and the Python log in one place.

IT CARRIES HEX, NOT INDIVIDUAL FIELDS. The sketch does not decode the packet: it
validates the framing (magic + version + CRC) and forwards all 39 bytes across as 78
hex characters. That keeps the packet layout in only TWO places already pinned to each
other -- the C struct and ``protocol.py`` -- rather than creating a third place inside
the sketch that has to be remembered. The reverse direction works the same way:
``protocol.build_command()`` packs and signs the CRC here, and the sketch only decodes
the hex and writes it straight out to the wire.

  A welcome side effect: ORG_ID does not need to appear in the sketch at all. The
  ``link_key`` hashed from it is computed in Python, so the app directory can be moved
  anywhere without carrying any household's identity with it.
"""

import asyncio
import logging
import time
from collections.abc import Callable

from edge_ai import protocol
from edge_ai.protocol import ProtocolError, Snapshot

logger = logging.getLogger("edge.bridge")

SnapshotHandler = Callable[[Snapshot], None]

# The RPC method names. They MUST MATCH the sketch -- a wrong name makes the router
# route into the void and REPORT NOTHING: the sending side's notify is
# fire-and-forget, and the receiving side simply never gets called. The symptom looks
# exactly like a broken cable.
RPC_SNAPSHOT = "gw/snapshot"
RPC_COMMAND = "gw/command"

# The gateway pushes a snapshot every 5 seconds. 30 seconds = six consecutive misses
# before we call it broken -- wide enough that recompiling the sketch (which restarts
# the STM32) is not read as a disconnection, narrow enough that we never command based
# on a reading a full minute old.
STALE_AFTER_SEC = 30.0


class GatewayLink:
    """Talks to the gateway over RouterBridge. The same interface as the pyserial version."""

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
        """Has the gateway been heard recently.

        It does NOT ask the socket's state: the socket to the router stays open even
        when the ESP32-S3 has been unplugged, so it would report "connected" for a dead
        path. The only thing that proves the whole chain is alive is a valid frame
        having just arrived.
        """
        return self._last_rx > 0.0 and (time.monotonic() - self._last_rx) < STALE_AFTER_SEC

    async def run(self, on_snapshot: SnapshotHandler) -> None:
        """Register the receiver and park here. Never returns.

        Unlike the pyserial version there is NO reopen loop: the router handles
        reconnection itself, and ``provide`` keeps the registration across sketch
        restarts. The loop below exists to report, not to repair.
        """
        self._loop = asyncio.get_running_loop()
        self._on_snapshot = on_snapshot

        from arduino.app_utils import Bridge  # only exists inside App Lab's container

        self._bridge = Bridge
        Bridge.provide(RPC_SNAPSHOT, self._on_frame)
        logger.info("Registered %s - waiting for the sketch to push snapshots", RPC_SNAPSHOT)

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
                "No snapshot in %.0fs. Check in this order:\n"
                "  1. Does the Sketch tab in App Lab show [rx] lines? If not, the UART\n"
                "     is dead - check ESP32-S3 GPIO18 -> D0, GPIO17 -> D1, and a common GND.\n"
                "  2. [rx] present but this line missing means RPC is dead - the method\n"
                "     name must be %r on both sides.",
                STALE_AFTER_SEC, RPC_SNAPSHOT,
            )

    # -- receiving -------------------------------------------------------------

    def _on_frame(self, hex_frame: str) -> None:
        """The sketch calls this. IT RUNS ON THE BRIDGE'S THREAD, not the asyncio thread.

        So it must not touch any of ``Controller``'s state -- neither ``RoomStore`` nor
        ``tick()`` has a lock, and mutating the history window while the control loop
        is iterating over it is the kind of bug that only appears after a long run.
        ``call_soon_threadsafe`` hands the work back to the right thread before calling.
        """
        try:
            raw = bytes.fromhex(hex_frame)
        except (ValueError, TypeError):
            self._note_bad("malformed hex")
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
        """The sketch has already checked the CRC, so a failure here is a failure AFTER
        the RPC hop.

        Only complain on the first of each streak: a version-mismatched packet would
        repeat every 5 seconds forever, and a log packed with one repeating line is the
        surest way to make sure nobody reads it any more.
        """
        self._bad += 1
        if self._bad == 1:
            logger.warning("The packet passed the sketch but failed here: %s", why)

    # -- sending ---------------------------------------------------------------

    async def send(self, *, kind: int, mode: protocol.Mode, setpoint: int | None) -> bool:
        """Send an advice or a command. Returns False if the chain is broken somewhere."""
        if self._bridge is None or not self.connected:
            return False

        self._seq = (self._seq + 1) & 0xFFFF
        frame = protocol.build_command(
            kind=kind, mode=mode, setpoint=setpoint, seq=self._seq, link_key=self._link_key
        )
        try:
            # notify rather than call: the sketch writes to the wire and that is all,
            # there is nothing to answer with, and a `call` would block the control loop
            # for 10 seconds every time the sketch is busy reading the UART.
            self._bridge.notify(RPC_COMMAND, frame.hex())
            return True
        except Exception as exc:  # noqa: BLE001
            logger.warning("Could not send the command over the bridge: %s", exc)
            return False
