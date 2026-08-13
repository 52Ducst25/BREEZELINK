"""Sinh toàn bộ icon của dự án từ một ảnh nguồn duy nhất.

    python scripts/generate-icons.py            # sinh lại tất cả
    python scripts/generate-icons.py --check    # chỉ báo cái nào lệch, không ghi

Nguồn: Icon/1.png. Đích: favicon web quản trị, launcher Android, AppIcon iOS, bộ
icon PWA, và icon tab của trang theo dõi Edge AI.

Sau khi chạy, phần Android cần thêm một bước vì nó có lớp adaptive riêng:

    cd app-flutter && dart run flutter_launcher_icons

GIỮ ĐÚNG KÍCH THƯỚC CŨ CỦA TỪNG FILE, không áp một bảng kích thước tự nghĩ ra.
Mỗi nền tảng có luật riêng — iOS đọc theo tên file, Android theo mật độ màn hình,
PWA theo manifest — và đổi kích thước một file là làm hỏng đúng nền tảng ấy theo
cách chỉ lộ ra lúc đóng gói, không phải lúc sinh.

Ô VUÔNG CẮT DÒ BẰNG HÀM KHOẢNG CÁCH, không ước lượng bằng mắt. Ảnh nguồn là một
khung cảnh rộng, khối bo tròn nằm lệch trái và góc phải có hai ngôi sao lấp lánh.
Lần dò đầu tiên lấy ngưỡng theo giá trị lớn nhất và bị hai ngôi sao đó kéo tâm
lệch 220px. Siết ngưỡng theo TỈ LỆ CHIỀU CAO mới tách được: khối cao ~1200px nên
mỗi cột của nó có rất nhiều điểm sáng, còn một ngôi sao chỉ cao vài chục px.
"""

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Thieu Pillow:  pip install Pillow")

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "Icon" / "1.png"

THRESH = 110          # nguong do sang tach khoi khoi nen
STEP = 2              # lay mau thua cho nhanh; bien khong can chinh xac tung diem
FILL = 0.72           # khoi chiem bao nhieu phan canh o vuong
ICO_SIZES = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]


def find_square(img: Image.Image) -> tuple[int, int, int]:
    """Ô vuông cắt: (left, top, cạnh), cân giữa quanh khối sáng."""
    w, h = img.size
    px = img.convert("L").load()

    col = [0] * w
    row = [0] * h
    for y in range(0, h, STEP):
        for x in range(0, w, STEP):
            if px[x, y] >= THRESH:
                col[x] += 1
                row[y] += 1

    def bounds(counts, min_hits, label):
        lo = next((i for i, c in enumerate(counts) if c >= min_hits), None)
        hi = next((i for i in range(len(counts) - 1, -1, -1) if counts[i] >= min_hits), None)
        if lo is None:
            sys.exit(f"Khong tim thay bien {label} — ha THRESH xuong")
        return lo, hi

    # Mot cot thuoc khoi phai sang tren it nhat 1/4 chieu cao. Ngoi sao lap lanh
    # cao ~60px tren anh 1728px => ~3%, roi xa duoi nguong nay.
    x0, x1 = bounds(col, (h / STEP) * 0.25, "ngang")
    y0, y1 = bounds(row, (w / STEP) * 0.15, "doc")

    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    side = min(int(max(x1 - x0, y1 - y0) / FILL), w, h)
    left = max(0, min(cx - side // 2, w - side))
    top = max(0, min(cy - side // 2, h - side))
    return left, top, side


def targets() -> list[Path]:
    """Mọi file icon ĐÃ CÓ. Kích thước lấy từ chính chúng — xem chú thích đầu file."""
    aflutter = REPO / "app-flutter"
    return [
        aflutter / "assets/icon/app_icon.png",
        aflutter / "web/favicon.png",
        *sorted((aflutter / "web/icons").glob("Icon-*.png")),
        *sorted((aflutter / "ios/Runner/Assets.xcassets/AppIcon.appiconset").glob("Icon-App-*.png")),
        REPO / "src/app/web/static/favicon-32.png",
        REPO / "src/app/web/static/favicon-180.png",
        REPO / "edge-ai/applab/BreezeLink/assets/favicon.png",
    ]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="chi bao, khong ghi de")
    args = ap.parse_args()

    if not SRC.is_file():
        sys.exit(f"Khong thay anh nguon: {SRC}")

    left, top, side = find_square(Image.open(SRC).convert("RGB"))
    master = Image.open(SRC).convert("RGB").crop((left, top, left + side, top + side))
    print(f"nguon {SRC.name} -> o vuong ({left}, {top}) canh {side}")

    corner = master.crop((0, 0, 40, 40)).resize((1, 1), Image.LANCZOS).getpixel((0, 0))
    print(f"mau goc (cho adaptive_icon_background): #{corner[0]:02X}{corner[1]:02X}{corner[2]:02X}\n")

    changed = 0
    for path in targets():
        if not path.is_file():
            print(f"  BO QUA (khong co): {path.relative_to(REPO)}")
            continue
        with Image.open(path) as im:
            w, h = im.size
        if w != h:
            print(f"  BO QUA (khong vuong {w}x{h}): {path.relative_to(REPO)}")
            continue
        if not args.check:
            master.resize((w, w), Image.LANCZOS).save(path, "PNG", optimize=True)
        changed += 1
        print(f"  {w:>4}px  {path.relative_to(REPO)}")

    # favicon.ico chua NHIEU CO trong mot file: 16 cho tab, 32 cho thanh dau
    # trang, 48 cho loi tat tren man hinh nen Windows. Chi mot co thi hai noi kia
    # phai phong to len va nhin nhoe.
    ico = REPO / "src/app/web/static/favicon.ico"
    if not args.check:
        master.resize((256, 256), Image.LANCZOS).save(ico, "ICO", sizes=ICO_SIZES)
    changed += 1
    print(f"  ICO     {ico.relative_to(REPO)} ({len(ICO_SIZES)} co)")

    print(f"\n{changed} file {'can sinh lai' if args.check else 'da sinh'}")
    if not args.check:
        print("Buoc tiep theo cho Android:")
        print("  cd app-flutter && dart run flutter_launcher_icons")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
