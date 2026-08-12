"""Dựng thư mục App của Arduino App Lab từ mã nguồn trong repo.

    python edge-ai/deploy/build-applab-app.py

Kết quả nằm ở ``edge-ai/applab/BreezeLink/`` — đẩy lên bo bằng:

    bash scripts/push-unoq-app.sh edge-ai/applab/BreezeLink

VÌ SAO PHẢI DỰNG CHỨ KHÔNG COMMIT SẴN: App Lab đòi cả cây mã nguồn nằm gọn
trong thư mục app (nó đóng gói nguyên thư mục rồi mới chạy), nhưng ``edge_ai``,
lát cắt ``app/`` của backend và header giao thức đều đã có chủ ở nơi khác trong
repo. Chép tay vào đây là tạo bản sao thứ hai, và bản sao sẽ trôi khỏi bản gốc —
đúng thứ mà chính ``comfort_bridge.py`` được viết ra để tránh.

CÁI GÌ LÀ MÃ NGUỒN, CÁI GÌ LÀ SẢN PHẨM (xem edge-ai/applab/.gitignore):

    BreezeLink/
    ├── app.yaml                      nguồn
    ├── sketch/sketch.ino             nguồn
    ├── sketch/sketch.yaml            nguồn
    ├── sketch/unoq-link-protocol.h   SINH RA  <- FirmWare/shared/
    ├── python/main.py                nguồn
    ├── python/requirements.txt       nguồn
    ├── python/.env                   cấu hình của hộ, KHÔNG vào git, KHÔNG bị xoá
    ├── python/edge_ai/               SINH RA  <- edge-ai/edge_ai/
    └── python/app/                   SINH RA  <- src/app/ (lát cắt comfort)
"""

import importlib.util
import shutil
import sys
import tempfile
from pathlib import Path


def _load(path: Path):
    """Nạp một file .py có gạch ngang trong tên (không import thẳng được)."""
    spec = importlib.util.spec_from_file_location(path.stem.replace("-", "_"), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Dùng lại đúng bộ dựng của bản systemd. Hai bản triển khai PHẢI mang cùng một
# lát cắt: một danh sách file thứ hai ở đây là chỗ để chúng lệch nhau, và triệu
# chứng sẽ là "chạy được bằng systemd mà không chạy được trong App Lab".
_payload = _load(Path(__file__).resolve().parent / "build-edge-payload.py")

REPO = Path(__file__).resolve().parents[2]
APP = REPO / "edge-ai" / "applab" / "BreezeLink"
SHARED_HEADER = REPO / "FirmWare" / "shared" / "unoq-link-protocol.h"

# Chỉ những thư mục này bị xoá và dựng lại. KHÔNG xoá cả python/: ở đó có
# main.py, requirements.txt và — quan trọng nhất — .env chứa ORG_ID của hộ, thứ
# không có bản nào khác để khôi phục.
_GENERATED = ["python/edge_ai", "python/app"]


def build() -> int:
    if not APP.is_dir():
        print(f"THIEU {APP} — day la ma nguon, khong phai san pham.", file=sys.stderr)
        return 1
    if not SHARED_HEADER.is_file():
        print(f"THIEU {SHARED_HEADER}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        staging = Path(tmp) / "payload"
        rc = _payload.build(staging)
        if rc != 0:
            return rc

        for rel in _GENERATED:
            dst = APP / rel
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(staging / Path(rel).name, dst)

    # Sketch cần header giao thức nằm CẠNH nó: arduino-cli chỉ biên dịch những gì
    # trong thư mục sketch, không có đường include nào trỏ ra ngoài repo bo.
    shutil.copy2(SHARED_HEADER, APP / "sketch" / SHARED_HEADER.name)

    n = sum(1 for _ in APP.rglob("*") if _.is_file())
    print(f"{APP}: {n} file")

    if not (APP / "python" / ".env").is_file():
        print(
            "\nCHUA CO python/.env — dich vu se dung ngay khi chay voi:\n"
            "    Thieu bien moi truong bat buoc: EDGE_ORG_ID\n"
            "Tao file do truoc khi day len bo (xem edge-ai/README.md).",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(build())
