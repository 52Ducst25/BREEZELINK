#!/usr/bin/env python3
# ============================================================================
#  Sinh font VLW (smooth font của TFT_eSPI) có ĐỦ DẤU TIẾNG VIỆT, xuất ra
#  header C để nhúng thẳng vào flash.
# ----------------------------------------------------------------------------
#  VÌ SAO KHÔNG DÙNG FONT GFX SẴN CÓ: font GFX (FreeSansBold12pt7b...) đánh chỉ
#  số theo MỘT BYTE, chỉ phủ ASCII 0x20-0x7E. Chữ "LÀM LẠNH" sẽ ra ô vuông hoặc
#  mất dấu. VLW đánh chỉ số theo mã Unicode nên chứa được 134 ký tự tiếng Việt.
#
#  VÌ SAO NHÚNG VÀO FLASH CHỨ KHÔNG ĐỂ SPIFFS: TFT_eSPI đọc được font từ mảng
#  PROGMEM (`loadFont(const uint8_t*)`). Nhúng thẳng thì không phải chia phân
#  vùng SPIFFS, không phải nhớ chạy thêm `pio run -t uploadfs` mỗi lần nạp —
#  đúng loại bước phụ mà người đi lắp sẽ quên.
#
#  CÁCH DÙNG:
#      python tools/make_vlw.py
#  rồi build lại. Chỉ cần chạy lại khi đổi cỡ chữ hoặc đổi bộ ký tự.
#
#  ĐỊNH DẠNG VLW (big-endian, đọc ngược từ Smooth_font.cpp của TFT_eSPI):
#      header : 6 x int32   gCount, version, size, 0, ascent, descent
#      metric : gCount x 7 x int32
#               unicode, height, width, xAdvance, dY, dX, 0
#      bitmap : nối tiếp, mỗi glyph width*height byte, 8-bit alpha
#  `dY` là khoảng cách từ ĐƯỜNG CƠ SỞ lên đỉnh glyph — TFT_eSPI vẽ tại
#  `y + maxAscent - dY`, sai chỗ này thì chữ nhảy hàng lung tung.
# ============================================================================
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "src", "ui", "fonts")

# Font nguồn: Arial có sẵn trên Windows và phủ đủ tiếng Việt.
# Đổi sang font khác thì nhớ kiểm tra nó có glyph cho các ký tự ở VIET bên dưới.
TTF_BOLD = r"C:\Windows\Fonts\arialbd.ttf"
TTF_REG  = r"C:\Windows\Fonts\arial.ttf"

# ---------------------------------------------------------------------------
#  Bộ ký tự
# ---------------------------------------------------------------------------
#  ASCII in được + ký hiệu độ. KHÔNG cắt bớt theo "những chữ giao diện đang
#  dùng": thêm một dòng chữ mới sau này mà thiếu glyph thì chữ biến mất trên
#  màn mà build vẫn xanh — lỗi im lặng, rất khó lần ra.
ASCII = "".join(chr(c) for c in range(0x20, 0x7F))
DEGREE = "\u00b0"

# 134 ký tự tiếng Việt dựng sẵn (67 hoa + 67 thường).
VIET = (
    "\u00c0\u00c1\u1ea2\u00c3\u1ea0"          # À Á Ả Ã Ạ
    "\u0102\u1eb0\u1eae\u1eb2\u1eb4\u1eb6"    # Ă Ằ Ắ Ẳ Ẵ Ặ
    "\u00c2\u1ea6\u1ea4\u1ea8\u1eaa\u1eac"    # Â Ầ Ấ Ẩ Ẫ Ậ
    "\u00c8\u00c9\u1eba\u1ebc\u1eb8"          # È É Ẻ Ẽ Ẹ
    "\u00ca\u1ec0\u1ebe\u1ec2\u1ec4\u1ec6"    # Ê Ề Ế Ể Ễ Ệ
    "\u00cc\u00cd\u1ec8\u0128\u1eca"          # Ì Í Ỉ Ĩ Ị
    "\u00d2\u00d3\u1ece\u00d5\u1ecc"          # Ò Ó Ỏ Õ Ọ
    "\u00d4\u1ed2\u1ed0\u1ed4\u1ed6\u1ed8"    # Ô Ồ Ố Ổ Ỗ Ộ
    "\u01a0\u1edc\u1eda\u1ede\u1ee0\u1ee2"    # Ơ Ờ Ớ Ở Ỡ Ợ
    "\u00d9\u00da\u1ee6\u0168\u1ee4"          # Ù Ú Ủ Ũ Ụ
    "\u01af\u1eea\u1ee8\u1eec\u1eee\u1ef0"    # Ư Ừ Ứ Ử Ữ Ự
    "\u1ef2\u00dd\u1ef6\u1ef8\u1ef4"          # Ỳ Ý Ỷ Ỹ Ỵ
    "\u0110"                                   # Đ
    "\u00e0\u00e1\u1ea3\u00e3\u1ea1"
    "\u0103\u1eb1\u1eaf\u1eb3\u1eb5\u1eb7"
    "\u00e2\u1ea7\u1ea5\u1ea9\u1eab\u1ead"
    "\u00e8\u00e9\u1ebb\u1ebd\u1eb9"
    "\u00ea\u1ec1\u1ebf\u1ec3\u1ec5\u1ec7"
    "\u00ec\u00ed\u1ec9\u0129\u1ecb"
    "\u00f2\u00f3\u1ecf\u00f5\u1ecd"
    "\u00f4\u1ed3\u1ed1\u1ed5\u1ed7\u1ed9"
    "\u01a1\u1edd\u1edb\u1edf\u1ee1\u1ee3"
    "\u00f9\u00fa\u1ee7\u0169\u1ee5"
    "\u01b0\u1eeb\u1ee9\u1eed\u1eef\u1ef1"
    "\u1ef3\u00fd\u1ef7\u1ef9\u1ef5"
    "\u0111"
)

CHARSET = ASCII + DEGREE + VIET


#  Font SỐ LỚN chỉ hiện nhiệt độ/setpoint nên KHÔNG cần glyph tiếng Việt —
#  cắt xuống còn chữ số + vài ký hiệu thì nó nhẹ đi khoảng 15 lần.
NUMSET = "0123456789.,-+%°C "


def build(px_size, charset, ttf):
    """Sinh một font VLW ở cỡ [px_size] pixel, trả về (bytes, thống kê)."""
    font = ImageFont.truetype(ttf, px_size)
    ascent, descent = font.getmetrics()

    metrics, bitmaps = [], []
    for ch in charset:
        cp = ord(ch)
        # Khoảng trắng không có pixel nào -> bitmap rỗng, chỉ giữ xAdvance.
        adv = int(round(font.getlength(ch)))
        bbox = font.getbbox(ch)          # (l, t, r, b) tính từ ĐỈNH DÒNG
        if bbox is None or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
            metrics.append((cp, 0, 0, adv, 0, 0))
            bitmaps.append(b"")
            continue

        l, t, r, b = bbox
        w, h = r - l, b - t
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-l, -t), ch, font=font, fill=255)

        # dY = khoảng cách từ đường cơ sở LÊN đỉnh glyph. Đường cơ sở nằm ở
        # `ascent` tính từ đỉnh dòng, còn đỉnh glyph ở `t`.
        metrics.append((cp, h, w, adv, ascent - t, l))
        bitmaps.append(img.tobytes())

    out = bytearray()
    out += struct.pack(">6i", len(metrics), 11, px_size, 0, ascent, descent)
    for cp, h, w, adv, dy, dx in metrics:
        out += struct.pack(">7i", cp, h, w, adv, dy, dx, 0)
    for bm in bitmaps:
        out += bm

    return bytes(out), (len(metrics), ascent, descent)


def write_header(path, symbol, blob, px_size, stats, ttf):
    n, ascent, descent = stats
    with open(path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("// SINH TỰ ĐỘNG bởi tools/make_vlw.py — ĐỪNG SỬA TAY.\n")
        f.write("// Chạy lại: python tools/make_vlw.py\n")
        f.write("//\n")
        f.write("// Font VLW (smooth font của TFT_eSPI), có đủ dấu tiếng Việt.\n")
        f.write("// Nguồn: %s @ %dpx · %d glyph · ascent %d · descent %d\n"
                % (os.path.basename(ttf), px_size, n, ascent, descent))
        f.write("// Nạp bằng: tft.loadFont(%s);\n" % symbol)
        f.write("#include <pgmspace.h>\n\n")
        f.write("const uint8_t %s[] PROGMEM = {\n" % symbol)
        for i in range(0, len(blob), 16):
            f.write("  " + ",".join("0x%02X" % b for b in blob[i:i + 16]) + ",\n")
        f.write("};\n")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for f in (TTF_BOLD, TTF_REG):
        if not os.path.exists(f):
            sys.exit("Khong tim thay font nguon: %s" % f)

    total = 0
    #  BA cỡ, không phải một: khi TFT_eSPI đã nạp smooth font thì setFreeFont()
    #  VÀ setTextFont(1) đều bị bỏ qua — MỌI chữ trên màn phải đến từ VLW. Nên
    #  phải tự dựng lại đủ phân cấp cỡ chữ mà bản dùng font GFX đang có.
    fonts = (
        # (cỡ px, tên biến C, bộ ký tự, file TTF)
        (13, "VietFontSmall", CHARSET, TTF_REG),   # nhãn phụ, thanh trạng thái
        (17, "VietFontLabel", CHARSET, TTF_BOLD),  # tiêu đề, nhãn nút
        (34, "VietFontBig",   NUMSET,  TTF_BOLD),  # số lớn (nhiệt độ, setpoint)
    )
    for px, sym, cs, ttf in fonts:
        blob, stats = build(px, cs, ttf)
        path = os.path.join(OUT_DIR, "%s.h" % sym.lower())
        write_header(path, sym, blob, px, stats, ttf)
        total += len(blob)
        print("%-14s %2dpx  %3d glyph  %6d byte  -> %s"
              % (sym, px, stats[0], len(blob), os.path.relpath(path, HERE)))
    print("Tong cong %d byte flash." % total)


if __name__ == "__main__":
    main()
