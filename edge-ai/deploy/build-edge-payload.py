"""Gom edge_ai/ + lát cắt thuật toán comfort thành một cây module chở đi được.

    python edge-ai/deploy/build-edge-payload.py [--out <thu-muc>]

`deploy-to-unoq.sh` gọi script này rồi chuyển kết quả sang Arduino UNO Q. Không
ai chạy nó tay, nhưng chạy được để xem lát cắt gồm những gì.

VÌ SAO PHẢI GOM THAY VÌ CHÉP CẢ src/:
  `comfort_bridge.py` cố ý import thuật toán comfort THẲNG từ `src/` của backend
  thay vì chép lại — hai bản sao sẽ lệch nhau và cho hai câu trả lời khác nhau
  cho "nhà này nên bao nhiêu độ". Ràng buộc đó đúng, nhưng nó buộc bản triển
  khai phải mang theo một lát cắt của backend.

  Chép NGUYÊN `src/` thì hỏng — đã thử trên bo thật:

      File ".../src/app/models/__init__.py", line 7
        from app.models.app_release import AppRelease
      ModuleNotFoundError: No module named 'sqlalchemy'

  `comfort_bridge` chỉ cần `app.models.enums.AcMode`, nhưng Python chạy
  `__init__.py` của gói TRƯỚC khi nạp module con, mà bản thật import cả 12 model
  ORM. Cài SQLAlchemy lên một thiết bị ngoài hiện trường để lấy đúng một enum là
  sai hướng, nên `app/models/__init__.py` được thay bằng bản rỗng.

CÂY KẾT QUẢ — một mục PYTHONPATH duy nhất làm cả hai import chạy được:

    payload/
    ├── edge_ai/     dịch vụ
    └── app/         comfort + enums + schemas
"""

import argparse
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
EDGE_PKG = REPO / "edge-ai" / "edge_ai"
BACKEND = REPO / "src" / "app"

# Đường dẫn tương đối trong src/app/ cần mang theo. Bám đúng chuỗi import thật:
# comfort_engine -> comfort/*, models/enums, schemas/comfort (một pydantic model).
_BACKEND_FILES = [
    "__init__.py",
    "comfort/__init__.py",
    "comfort/comfort_constants.py",
    "comfort/comfort_engine.py",
    "comfort/ir_snap.py",
    "comfort/mode_decision.py",
    "comfort/room_aggregate.py",
    "comfort/running_mean.py",
    "comfort/setpoint_calculator.py",
    "models/enums.py",
    "schemas/__init__.py",
    "schemas/comfort.py",
]

_MODELS_INIT = '''"""Bản RỖNG có chủ đích — không phải bản của backend.

Bản thật ở src/app/models/__init__.py import cả 12 model ORM để Alembic thấy
chúng trên Base.metadata. Ở đây chỉ cần `app.models.enums.AcMode`, mà Python
chạy file __init__.py của gói trước khi nạp module con — nên chép bản thật lên
là kéo theo SQLAlchemy và toàn bộ ORM lên một thiết bị chỉ cần một enum.

Sinh bởi edge-ai/deploy/build-edge-payload.py. Đừng sửa tay.
"""
'''


def _copy(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def build(out: Path) -> int:
    # Xoá sạch rồi dựng lại: một file đã bỏ khỏi dịch vụ mà còn sót lại trong
    # payload vẫn import được, và "đã xoá rồi mà lỗi cũ vẫn còn" là kiểu hỏng tốn
    # nhiều thời gian nhất để tìm.
    if out.exists():
        shutil.rmtree(out)

    n = 0
    for item in sorted(EDGE_PKG.glob("*.py")):
        _copy(item, out / "edge_ai" / item.name)
        n += 1
    for rel in _BACKEND_FILES:
        src = BACKEND / rel
        if not src.is_file():
            print(f"THIEU {src} — lat cat khong con khop voi backend.", file=sys.stderr)
            return 1
        _copy(src, out / "app" / rel)
        n += 1

    (out / "app" / "models" / "__init__.py").write_text(_MODELS_INIT, encoding="utf-8")
    print(f"{out}: {n + 1} file")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path,
                    default=Path(__file__).resolve().parent / "payload",
                    help="thu muc ket qua (mac dinh: edge-ai/deploy/payload)")
    raise SystemExit(build(ap.parse_args().out))
