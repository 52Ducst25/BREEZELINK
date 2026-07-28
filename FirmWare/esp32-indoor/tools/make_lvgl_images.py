# ============================================================================
#  Chuyển ảnh PNG -> mảng C của LVGL, xuất ra src/ui/images/*.c
# ----------------------------------------------------------------------------
#  Chạy:  python tools\make_lvgl_images.py
#  Cần:   pip install pillow
#
#  VÌ SAO TỰ VIẾT thay vì dùng lv_img_conv: công cụ npm đó bám theo từng đời
#  LVGL, và bản khớp 8.3 không phải bản mới nhất — cài nhầm là ra header sai
#  lặng lẽ. Định dạng ảnh của LVGL 8.3 đủ đơn giản để sinh thẳng, và làm vậy thì
#  cờ LV_COLOR_16_SWAP nằm ngay trong tầm kiểm soát (xem dưới).
#
#  BẪY LỚN NHẤT — THỨ TỰ BYTE: lv_conf.h đặt LV_COLOR_16_SWAP=1 (TFT_eSPI cần
#  vậy). Ảnh phải được ghi CÙNG thứ tự đó, nếu không ảnh hiện ra đúng hình nhưng
#  SAI MÀU (xanh ra cam) — trông y như hỏng phần cứng nên rất dễ tìm nhầm chỗ.
#
#  Ảnh nền KHÔNG có alpha (TRUE_COLOR, 2 byte/điểm) vì nó phủ kín màn; icon thì
#  CÓ alpha (TRUE_COLOR_ALPHA, 3 byte/điểm) để lọt nền phía sau.
# ============================================================================
import io
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "Interface", "LVGL", "assets")
OUT = os.path.join(HERE, "..", "src", "ui", "images")

# (file nguồn, tên biến, kích thước đích, có alpha)
#
# GIỮ ĐÚNG TỈ LỆ GỐC, nếu không ảnh bị bóp méo mà không ai báo lỗi:
#   12.png               320x240 -> 320x240  (khít màn, tỉ lệ 4:3 sẵn)
#   air-conditioning.png 148x60  -> 44x18    (148/60 = 2.47 ~ 44/18)
#   plus / min_nimum      46x46  -> 30x30    (vuông; 30 px cho nút 68x76 —
#                                             22 px trông lọt thỏm)
JOBS = [
    ("12.png",               "img_bg_tech",  (320, 240), False),
    ("air-conditioning.png", "img_ac_unit",  (44, 18) ,  True),
    ("plus.png",             "img_plus",     (30, 30),   True),
    ("min_nimum.png",        "img_minus",    (30, 30),   True),
]


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def convert(path, name, size, with_alpha):
    im = Image.open(path).convert("RGBA")
    if size:
        im = im.resize(size, Image.LANCZOS)
    w, h = im.size
    px = im.load()

    body = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            v = rgb565(r, g, b)
            # LV_COLOR_16_SWAP=1 -> byte cao trước. Xem ghi chú ở đầu file.
            body += bytes((v >> 8, v & 0xFF))
            if with_alpha:
                body.append(a)

    cf = "LV_IMG_CF_TRUE_COLOR_ALPHA" if with_alpha else "LV_IMG_CF_TRUE_COLOR"
    out = io.StringIO()
    out.write("// SINH TU ĐONG bởi tools/make_lvgl_images.py — ĐỪNG SỬA TAY.\n")
    out.write(f"// Nguồn: Interface/LVGL/assets/{os.path.basename(path)}  ({w}x{h}, "
              f"{'có' if with_alpha else 'không'} alpha)\n")
    out.write("#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include \"lvgl.h\"\n"
              "#else\n#include \"lvgl/lvgl.h\"\n#endif\n\n")
    out.write("#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif\n\n")
    out.write(f"const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}_map[] = {{\n")
    for i in range(0, len(body), 16):
        out.write("    " + ", ".join(f"0x{b:02x}" for b in body[i:i + 16]) + ",\n")
    out.write("};\n\n")
    out.write(f"const lv_img_dsc_t {name} = {{\n")
    out.write(f"    .header.cf = {cf},\n")
    out.write("    .header.always_zero = 0,\n    .header.reserved = 0,\n")
    out.write(f"    .header.w = {w},\n    .header.h = {h},\n")
    out.write(f"    .data_size = {len(body)},\n    .data = {name}_map,\n}};\n")
    return out.getvalue(), len(body)


def main():
    os.makedirs(OUT, exist_ok=True)
    total = 0
    for fname, name, size, alpha in JOBS:
        p = os.path.join(SRC, fname)
        if not os.path.exists(p):
            print(f"BO QUA (khong thay): {fname}")
            continue
        code, nbytes = convert(p, name, size, alpha)
        dst = os.path.join(OUT, f"{name}.c")
        with io.open(dst, "w", encoding="utf-8") as f:
            f.write(code)
        total += nbytes
        print(f"{name:<14} {nbytes/1024:7.1f} KB flash   <- {fname}")
    print(f"{'TONG':<14} {total/1024:7.1f} KB flash")


if __name__ == "__main__":
    main()
