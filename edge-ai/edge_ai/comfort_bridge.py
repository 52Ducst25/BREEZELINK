"""Single import point for the backend's comfort algorithm.

WHY A BRIDGE INSTEAD OF A COPY: ``src/app/comfort/`` is pure — no DB, no Redis,
no MQTT — so it imports and runs unchanged on the UNO Q. Re-implementing the
setpoint maths here would create two answers to "what temperature should this
house be", drifting apart with every backend change, and the symptom of that
drift is an air conditioner running the wrong setpoint in someone's home. The
project audit already names untested comfort code as its number-one risk;
duplicating it would double that risk instead of halving it.

The path insert is the price of a repo laid out as ``src/`` + ``edge-ai/``
rather than one installable package. It is done once, here, so no other module
in this service needs to know where the backend lives — and ``EDGE_BACKEND_SRC``
overrides it for a deployment that ships only part of the tree.
"""

import importlib.util
import os
import sys
from pathlib import Path


def _comfort_already_importable() -> bool:
    """Is `app.comfort` already importable from sys.path."""
    try:
        return importlib.util.find_spec("app.comfort") is not None
    except (ImportError, ValueError):
        return False


# HỎI TRÌNH NẠP MODULE TRƯỚC, ĐỪNG HỎI HỆ THỐNG TỆP.
#
# An earlier version required `EDGE_BACKEND_SRC` to be a REAL DIRECTORY containing
# app/comfort. Correct for an install that rsyncs the whole `src/`, but it locked out
# every other deployment style. The approach actually in use ships a SLICE of `app/`
# sitting next to `edge_ai/` in the same PYTHONPATH entry
# (deploy/build-edge-payload.py) -- in which case `app.comfort` imports fine but there
# is NO directory called `src/` to check, and the old check blocked precisely what it
# had no reason to block. Zipimport is the same: importable from inside a .zip, while
# `Path.is_dir()` always returns False for that path.
#
# If `app.comfort` already imports, no environment variable is needed at all. Only
# when it does NOT import do we go looking by path, and there the directory check
# still earns its keep: it turns a baffling ModuleNotFoundError into a sentence saying
# exactly what to fix.
if not _comfort_already_importable():
    _DEFAULT_SRC = Path(__file__).resolve().parents[2] / "src"
    _BACKEND_SRC = Path(os.getenv("EDGE_BACKEND_SRC", str(_DEFAULT_SRC)))

    if not (_BACKEND_SRC / "app" / "comfort").is_dir():
        raise ImportError(
            f"The comfort algorithm was not found at {_BACKEND_SRC}. "
            "Set EDGE_BACKEND_SRC to point at the backend's src/ directory."
        )
    if str(_BACKEND_SRC) not in sys.path:
        sys.path.insert(0, str(_BACKEND_SRC))

# ruff: noqa: E402  — the path insert above must run before these imports.
from app.comfort.comfort_constants import ComfortConfig  # type: ignore
from app.comfort.comfort_engine import ComfortInputs, compute  # type: ignore
from app.comfort.ir_snap import NoIrCodesError  # type: ignore
from app.comfort.room_aggregate import RoomAggregate, RoomReading  # type: ignore
from app.comfort.room_aggregate import aggregate as aggregate_rooms  # type: ignore
from app.models.enums import AcMode  # type: ignore

__all__ = [
    "AcMode",
    "ComfortConfig",
    "ComfortInputs",
    "NoIrCodesError",
    "RoomAggregate",
    "RoomReading",
    "aggregate_rooms",
    "compute",
]
