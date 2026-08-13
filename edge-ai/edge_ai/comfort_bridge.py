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
    """`app.comfort` đã nằm sẵn trên sys.path chưa."""
    try:
        return importlib.util.find_spec("app.comfort") is not None
    except (ImportError, ValueError):
        return False


# HỎI TRÌNH NẠP MODULE TRƯỚC, ĐỪNG HỎI HỆ THỐNG TỆP.
#
# Bản trước bắt buộc `EDGE_BACKEND_SRC` phải là một THƯ MỤC THẬT chứa app/comfort.
# Đúng cho bản cài bằng rsync cả `src/`, nhưng nó khoá chặt mọi cách triển khai
# khác. Cách đang dùng thật là chở một LÁT CẮT `app/` nằm ngay cạnh `edge_ai/`
# trong cùng một mục PYTHONPATH (deploy/build-edge-payload.py) — lúc đó
# `app.comfort` nạp được nhưng KHÔNG có thư mục nào tên `src/` để mà kiểm, và
# phép kiểm cũ chặn đúng cái nó không có lý do gì để chặn. Zipimport cũng vậy:
# nạp được từ trong .zip, mà `Path.is_dir()` luôn trả False cho đường dẫn đó.
#
# Nếu `app.comfort` đã nạp được rồi thì không cần biến môi trường nào cả. Chỉ khi
# CHƯA nạp được mới đi tìm theo đường dẫn, và lúc đó phép kiểm thư mục vẫn còn
# nguyên giá trị: nó biến một ModuleNotFoundError khó hiểu thành một câu nói rõ
# phải sửa gì.
if not _comfort_already_importable():
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
