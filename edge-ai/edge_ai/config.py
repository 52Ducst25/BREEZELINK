"""Configuration for the edge-AI service, read from the environment.

Nothing here has a secret as its default. The service refuses to start rather
than fall back to a guess for ``EDGE_ORG_ID`` — that value is hashed into the
``link_key`` the gateway checks, so a wrong one means every command this node
sends is silently rejected, with the gateway logging "another household's UNO Q?"
and this node logging nothing at all.
"""

import os
from dataclasses import dataclass
from pathlib import Path


class ConfigError(RuntimeError):
    """Raised at startup for a missing or nonsensical setting."""


def _require(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise ConfigError(f"Missing required environment variable: {name}")
    return value


def _num(name: str, default: float) -> float:
    raw = os.getenv(name, "").strip()
    if not raw:
        return default
    try:
        return float(raw)
    except ValueError as exc:
        raise ConfigError(f"{name} must be a number, got {raw!r}") from exc


def _coords() -> tuple[float | None, float | None]:
    """Coordinates for the forecast. BOTH must be present, or neither.

    Declaring only one is a configuration error rather than "disabled": it is almost
    certainly a typo, and silently ignoring it would leave a forecast that never runs
    with nobody understanding why.
    """
    lat_raw = os.getenv("EDGE_LAT", "").strip()
    lon_raw = os.getenv("EDGE_LON", "").strip()
    if not lat_raw and not lon_raw:
        return None, None
    if not lat_raw or not lon_raw:
        raise ConfigError("EDGE_LAT and EDGE_LON must be declared together, or both left empty")
    try:
        lat, lon = float(lat_raw), float(lon_raw)
    except ValueError as exc:
        raise ConfigError(f"EDGE_LAT/EDGE_LON must be numbers: {lat_raw!r}, {lon_raw!r}") from exc
    if not (-90 <= lat <= 90 and -180 <= lon <= 180):
        raise ConfigError(f"Coordinates out of range: {lat}, {lon}")
    return lat, lon


def _ir_temps() -> tuple[int, ...]:
    """Read EDGE_IR_TEMPS="24,25,26,27,28". Empty = learn by observation.

    Nonsensical values are rejected rather than silently filtered: a level of 250
    getting in here would be treated by ``nearest_captured_temp`` as a valid
    candidate, and since the gateway has no code for it the command goes out and
    falls into the void -- with the only symptom being "the air conditioner does not
    move".
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
            raise ConfigError(f"EDGE_IR_TEMPS contains a non-numeric entry: {part!r}") from exc
        if not 5 <= value <= 35:
            raise ConfigError(f"EDGE_IR_TEMPS: {value}°C is outside the domestic air conditioner range")
        out.append(value)
    return tuple(sorted(set(out)))


def _link() -> str:
    """Pick the path to the gateway. "auto" = detect whether we are inside App Lab.

    The tell is THE SOCKET, not whether the `arduino` package imports: that package
    ships in the base image so it is present even when the router is not running, and
    in that case `Bridge.provide()` raises a hard-to-read connection error instead of
    falling back to pyserial.
    """
    choice = os.getenv("EDGE_LINK", "auto").strip().lower() or "auto"
    if choice in {"bridge", "serial"}:
        return choice
    if choice != "auto":
        raise ConfigError(f"EDGE_LINK must be auto|bridge|serial, got {choice!r}")
    return "bridge" if os.path.exists("/var/run/arduino-router.sock") else "serial"


@dataclass(frozen=True)
class Settings:
    org_id: str

    # The UART port to the gateway. None = auto-detect (preferring CH34x/CP210x/FTDI
    # bridge chips). Declare it by hand when the UART pins are wired directly: Linux
    # cannot guess which /dev/ttyS* is connected to what.
    #
    # (EDGE_SCAN_TIMEOUT_SEC went away along with the `scan_timeout_sec` field: that
    # was the BLE scan timeout for finding the gateway by service UUID. Since the
    # move to UART there is nothing to scan -- the field was only ever SET, never
    # READ anywhere.)
    uart_port: str | None
    reconnect_sec: float

    # The path to the gateway: "bridge" (through the sketch on the STM32) or "serial"
    # (the Linux half's USB-TTL port). "auto" picks bridge when running inside App
    # Lab.
    #
    # THESE TWO CANNOT BOTH BE RIGHT: the cable can only be plugged into one place.
    # Plugged into D0/D1 there is NO corresponding /dev/tty* on the Linux half --
    # pyserial would grab some other port at random and then wait forever, and the
    # symptom ("no snapshots") looks exactly like a broken cable. Hence detection
    # rather than guessing by default.
    link: str

    # The sliding window of per-corner readings (seconds). Long enough for a linear
    # regression to have something to hold on to, short enough that an afternoon of
    # harsh sun is not diluted by a cool morning.
    history_sec: float

    # How long the server has to be silent before the UNO Q takes control.
    #
    # This number is compared against the `cloud_silence_sec` THE GATEWAY REPORTS,
    # not against a measurement made by this service -- the gateway holds the MQTT
    # session so it knows more reliably.
    #
    # 300s is deliberately generous: both sides issuing commands means the air
    # conditioner receives two contradictory orders within the same minute, and the
    # symptom (a setpoint that jumps on its own) looks exactly like an algorithm bug.
    # Better to take over late.
    takeover_after_sec: float

    # The control loop interval. It does not need to be short: room temperature does
    # not change over 30 seconds, and every iteration can emit an IR command.
    tick_sec: float

    # The temperatures this household HAS LEARNED IR CODES for (COOL mode).
    #
    # IT HAS TO BE DECLARED BY HAND because the edge node has no path to Postgres --
    # the `ir_codes` table lives on the server, and the entire reason this node exists
    # is to keep running when the server is gone. Left empty it falls back to the old
    # approach: learning by watching the server issue commands, and that approach has
    # a trap:
    #
    #   Freshly started, it has seen exactly one level (say 27).
    #   nearest_captured_temp() always returns the NEAREST level in the list it has --
    #   so a target of 24.5 gets forced to 27, with no error and no warning. A
    #   half-filled list is more dangerous than an empty one: an empty one makes
    #   NoIrCodesError stop everything.
    #
    # Get the real list with:
    #   SELECT DISTINCT temp FROM ir_codes WHERE mode='COOL' ORDER BY temp;
    ir_temps: tuple[int, ...]

    # Local history (SQLite). This is the foundation of the thermal model: without it,
    # every service restart puts the model back to zero and it takes hours to become
    # usable again. Empty = next to the edge_ai package.
    history_db: str
    history_days: float

    # Coordinates for fetching the outdoor temperature forecast (open-meteo, no API
    # key needed). Both None = the forecast is disabled; the model still runs but
    # looking further ahead than ~15 minutes it has to assume the outdoor temperature
    # stands still.
    lat: float | None
    lon: float | None

    # Advise only, never command -- even when the cloud is gone. Enable it during a
    # trial run in a customer's home to compare edge decisions against cloud
    # decisions before handing it real authority.
    advisory_only: bool


def _default_db() -> str:
    """The ``data/`` directory next to the edge_ai package.

    IT HAS TO BE INSIDE THE APP DIRECTORY, with no alternative: App Lab's container
    only mounts one location that is both writable and survives a rebuild -- the app
    directory itself (``/app``). Every other path vanishes with the container.

    IN ITS OWN SUBDIRECTORY, not scattered next to the source. SQLite creates THREE
    files (``.db``, ``.db-wal``, ``.db-shm``), plus the forecast cache makes four.
    Sitting alongside ``main.py``, App Lab's file browser lists them mixed in with the
    source, and clicking a binary file by mistake opens it in the text editor --
    saving it once corrupts the whole database.
    """
    return str(Path(__file__).resolve().parent.parent / "data" / "breezelink-history.db")


def load() -> Settings:
    """Read the configuration from the environment (with .env already loaded if present)."""
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
        reconnect_sec=_num("EDGE_RECONNECT_SEC", 5),
        history_sec=_num("EDGE_HISTORY_SEC", 1800),
        takeover_after_sec=_num("EDGE_TAKEOVER_AFTER_SEC", 300),
        tick_sec=_num("EDGE_TICK_SEC", 30),
        advisory_only=os.getenv("EDGE_ADVISORY_ONLY", "").strip().lower()
        in {"1", "true", "yes"},
    )
