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

import os
import sys
from pathlib import Path

_DEFAULT_SRC = Path(__file__).resolve().parents[2] / "src"
_BACKEND_SRC = Path(os.getenv("EDGE_BACKEND_SRC", str(_DEFAULT_SRC)))

if not (_BACKEND_SRC / "app" / "comfort").is_dir():
    raise ImportError(
        f"Không tìm thấy thuật toán comfort ở {_BACKEND_SRC}. "
        "Đặt EDGE_BACKEND_SRC trỏ tới thư mục src/ của backend."
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
