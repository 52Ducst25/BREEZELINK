"""Packet layout for the UART link to the gateway.

THE TWIN OF ``Firmware/shared/unoq-link-protocol.h``. The ``struct`` format strings
below must match the C structs over there byte for byte; change one side and forget
the other and the packet still decodes "successfully" while producing garbage
temperatures -- hence ``VERSION``, which is checked at every entry point.

Why it is not generated automatically from the C header: a code generator is another
build step that has to run correctly on both the dev machine and the UNO Q, all to
avoid copying twenty lines. That price is not worth paying; the price of forgetting
to sync is covered by the version field plus the CRC.
"""

import struct
from dataclasses import dataclass
from enum import IntEnum

MAGIC = 0xAC
# 2 = the UART version. Version 1 was BLE and is NOT compatible: a v1 command packet
# is 12 bytes, v2 is 13 (a crc8 was added). The gateway checks this field, so a
# version mismatch is rejected outright rather than misreading fields and issuing a
# wrong command.
VERSION = 2

# UART speed. It MUST MATCH UNOQ_BAUD in Firmware/esp32-*/platformio.ini.
# 115200 is more than enough: a 39-byte snapshot every 5 seconds = 62 bytes/second.
# Do not raise it -- the wire between the two boards is unshielded, and a higher rate
# trades bit error rate for nothing at all at this throughput. A baud mismatch
# produces garbage bytes, and the symptom looks exactly like a missing GND wire.
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
    """Matches AcUnoQMode in the firmware AND app.models.enums.AcMode in the backend."""

    OFF = 0
    COOL = 1
    DRY = 2
    FAN = 3
    UNKNOWN = 0xFF

    @property
    def name_str(self) -> str:
        return "UNKNOWN" if self is Mode.UNKNOWN else self.name


# '<' = little-endian with NO alignment -- mandatory, because the C struct on the
# other side is __packed__. Drop the '<' and Python inserts padding bytes, shifting
# every field after the first one.
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
# THE CRC IS NEW IN THE UART VERSION. The BLE version had none, because Bluetooth's
# link layer already checked a CRC24 and discarded corrupt packets before they
# reached the application layer. UART has nothing of the kind: a noisy wire,
# hot-plugging the cable, a baud mismatch -- all of them push garbage bytes straight
# up. For a snapshot, one flipped bit corrupts one reading for one tick; for a
# COMMAND it changes the setpoint and goes straight out to the air conditioner.
_COMMAND_FMT = "<BBBBbBHIB"
COMMAND_SIZE = struct.calcsize(_COMMAND_FMT)

# Size pinning, matching the static_assert in unoq-link-protocol.h. Add a field on
# one side and forget the other and the packet still "decodes successfully" -- only
# every field after the insertion point is shifted, and the room temperature reads
# out as garbage. The CRC does not save you: it is computed over exactly the number
# of bytes the sender thought was right. These two lines blow up at import time,
# i.e. when the service starts, rather than while it is driving an air conditioner.
assert SNAPSHOT_SIZE == 39, f"Snapshot is {SNAPSHOT_SIZE} bytes, the firmware pins 39"
assert COMMAND_SIZE == 13, f"Command is {COMMAND_SIZE} bytes, the firmware pins 13"


class ProtocolError(ValueError):
    """The packet is malformed -- the caller must DISCARD it and must not guess."""


def crc8(data: bytes) -> int:
    """CRC8 Dallas/Maxim (reflected polynomial 0x8C) -- matches acUnoQCrc8()."""
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
    """FNV-1a 32-bit -- matches acUnoQLinkKey(). Used to hash ORG_ID into link_key."""
    h = 2166136261
    for b in text.encode("utf-8"):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def _temp(raw: int) -> float | None:
    """None rather than 0.0 for "no reading": 0 degC is a valid temperature, and
    reading it as "missing" is the fastest way for a broken sensor to drag the
    median."""
    return None if raw == T_INVALID else raw / 100.0


def _rh(raw: int) -> float | None:
    return None if raw == H_INVALID else raw / 100.0


@dataclass(frozen=True)
class RoomSlot:
    corner: int | None  # the corner label the node declares; None = empty slot
    temp: float | None
    humidity: float | None


@dataclass(frozen=True)
class Snapshot:
    """The snapshot the gateway sends over. Every missing reading is None, never 0."""

    room_count: int
    flags: int
    t_in: float | None
    h_in: float | None
    t_out: float | None
    h_out: float | None
    rooms: list[RoomSlot]
    cloud_silence_sec: int | None  # None = the server has NEVER issued a command
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
    """Decode one snapshot. Raises ProtocolError if anything does not match.

    THE LENGTH IS CHECKED FIRST: over UART a frame can arrive in several pieces (the
    driver buffer splits anywhere). The caller must gather enough bytes before
    calling in here; this check turns "called too early" into a readable message
    rather than an IndexError.
    """
    if len(data) < SNAPSHOT_SIZE:
        raise ProtocolError(
            f"Snapshot is {len(data)} bytes, {SNAPSHOT_SIZE} are needed. "
            "Almost certainly the MTU is too small and the packet was truncated."
        )
    fields = struct.unpack(_SNAPSHOT_FMT, data[:SNAPSHOT_SIZE])

    magic, version, room_count, flags = fields[0:4]
    if magic != MAGIC:
        raise ProtocolError(f"wrong magic: {magic:#04x}")
    if version != VERSION:
        raise ProtocolError(
            f"packet version {version}, this service understands {VERSION} -- "
            "the gateway firmware and edge-ai have diverged, reflash one of them."
        )
    if crc8(data[: SNAPSHOT_SIZE - 1]) != fields[-1]:
        raise ProtocolError("wrong CRC")

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
    """Package an advice (kind=ADVICE) or a real command (kind=COMMAND).

    ``kind`` is the most important boundary in the whole protocol: the gateway ONLY
    fires infrared on COMMAND. The default at every call site must be ADVICE.
    """
    # Pack with crc8 = 0, then compute the CRC over the first 12 bytes and overwrite
    # the last one. The same way the firmware does it (acUnoQSealCommand) -- and it
    # has to be the same way, because a CRC computed over a byte string that differs
    # by one byte makes the gateway reject every command, with the symptom being
    # simply "the air conditioner does not move".
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
        0,                    # crc8, filled in just below
    )
    return body[:-1] + bytes([crc8(body[:-1])])
