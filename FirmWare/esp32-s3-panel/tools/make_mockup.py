# ============================================================================
#  Sinh Interface/mockup.svg — xem trước giao diện TRƯỚC KHI nạp lên phần cứng
# ----------------------------------------------------------------------------
#  Chạy:  python tools\make_mockup.py
#  Cần:   pip install pillow
#
#  VÌ SAO LÀ SCRIPT CHỨ KHÔNG PHẢI FILE SVG VIẾT TAY: bản trước tôi vẽ tay, và
#  nó lệch khỏi code ngay lần đổi bố cục đầu tiên — một cái mockup nói dối còn
#  tệ hơn không có mockup. Ở đây toạ độ được chép thẳng từ ui-screens.cpp và để
#  cạnh nhau trong PANELS, nên lệch là thấy ngay.
#
#  ĐÂY KHÔNG PHẢI TRÌNH GIẢ LẬP LVGL. Nó dựng lại bố cục bằng SVG, đủ để duyệt
#  bố cục / màu / độ tương phản. Ba thứ nó KHÔNG mô phỏng được:
#    - bề rộng chữ thật của font Arial đã sinh (SVG dùng Arial của máy — gần
#      đúng, không đúng tuyệt đối)
#    - phối màu RGB565 (mockup là 24-bit, panel thật là 16-bit -> chuyển màu
#      trong gradient nền sẽ hơi vằn)
#    - độ sáng thực của tấm nền TFT
# ============================================================================
import base64
import io
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BG = os.path.join(HERE, "..", "..", "Interface", "LVGL", "assets", "12.png")
OUT = os.path.join(HERE, "..", "..", "Interface", "mockup.svg")

S = 2                      # phóng 2x cho dễ nhìn trên màn máy tính
W, H = 320, 240

# --- Bảng màu: PHẢI khớp src/ui/theme.h ---------------------------------------
C = dict(
    bg="#0C193B", card="#141C28", subtle="#121924",
    text="#E7F1F8", muted="#8DA2B5",
    bSubtle="#2A3B4C", bDefault="#3E5468",
    accent="#0055FF", accentText="#4D8DFF",
    ok="#22C55E", err="#FF4D4D", warn="#F5A623",
)
CARD_OPA, BORDER_OPA, BTN_OPA = 216 / 255, 180 / 255, 232 / 255

out = []


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def chamfer_pts(x, y, w, h, c):
    """Đường bao vát 45° hai góc TRÊN-TRÁI + DƯỚI-PHẢI — chữ ký của hệ thiết kế."""
    return " ".join(f"{a},{b}" for a, b in [
        (x + c, y), (x + w, y), (x + w, y + h - c),
        (x + w - c, y + h), (x, y + h), (x, y + c)])


def rect(x, y, w, h, fill, opa=1.0, stroke=None, sw=1, ch=0, sopa=1.0):
    if ch:
        out.append(f'<polygon points="{chamfer_pts(x,y,w,h,ch)}" fill="{fill}" '
                   f'fill-opacity="{opa}"'
                   + (f' stroke="{stroke}" stroke-width="{sw}" stroke-opacity="{sopa}"'
                      if stroke else "") + "/>")
    else:
        out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" '
                   f'fill-opacity="{opa}"'
                   + (f' stroke="{stroke}" stroke-width="{sw}" stroke-opacity="{sopa}"'
                      if stroke else "") + "/>")


def text(x, y, s, size=12, fill=None, anchor="start", weight="normal", ls=0, opa=1.0):
    # LVGL đặt nhãn theo GÓC TRÊN-TRÁI, SVG đặt theo ĐƯỜNG CHÂN CHỮ. 0.78*size là
    # phần thân trên của Arial — không quy đổi thì mọi dòng chữ trong mockup nằm
    # cao hơn thực tế đúng một khoảng bằng chiều cao chữ.
    out.append(f'<text x="{x}" y="{y + size * 0.78:.1f}" font-family="Arial,Helvetica" '
               f'font-size="{size}" fill="{fill or C["text"]}" fill-opacity="{opa}" '
               f'text-anchor="{anchor}" font-weight="{weight}" '
               f'letter-spacing="{ls}">{esc(s)}</text>')


def card(x, y, w, h, ch=6):
    rect(x, y, w, h, C["card"], CARD_OPA, C["bDefault"], 1, ch, BORDER_OPA)


def button(x, y, w, h, lbl, kind="normal", size=12):
    """kind: normal | primary | selected | off"""
    fill, opa, stroke = C["card"], BTN_OPA, C["bDefault"]
    tc = C["text"]
    if kind in ("primary", "selected"):
        fill, opa, stroke = C["accent"], 1.0, C["accentText"]
    elif kind == "off":
        fill, opa, stroke, tc = C["subtle"], 140 / 255, C["bSubtle"], C["muted"]
    rect(x, y, w, h, fill, opa, stroke, 2, 4)
    text(x + w / 2, y + h / 2 - size * 0.6, lbl, size, tc, "middle", ls=1)


def dot(x, y, col):
    out.append(f'<circle cx="{x+3}" cy="{y+3}" r="3.5" fill="{col}"/>')


def chrome(active, clock="14:32", wifi=True, mqtt=True):
    """Thanh trạng thái + thanh điều hướng — có ở MỌI màn."""
    rect(0, 0, W, 22, C["card"], 1.0)
    out.append(f'<line x1="0" y1="22" x2="{W}" y2="22" stroke="{C["bSubtle"]}" stroke-width="1"/>')
    rect(6, 6, 9, 9, C["accent"], 1.0)
    text(20, 4, "AIRCON", 12, C["accent"], ls=1)
    dot(238, 7, C["ok"] if wifi else C["err"])
    dot(252, 7, C["ok"] if mqtt else C["err"])
    text(270, 4, clock, 12)

    rect(0, 206, W, 34, C["card"], 1.0)
    out.append(f'<line x1="0" y1="206" x2="{W}" y2="206" stroke="{C["bSubtle"]}" stroke-width="1"/>')
    for i, t in enumerate(["TRANG CHỦ", "ĐIỀU KHIỂN", "THÔNG TIN", "CÀI ĐẶT"]):
        if i == active:
            rect(80 * i, 206, 80, 3, C["accent"], 1.0)
        text(80 * i + 40, 215, t, 12,
             C["accent"] if i == active else C["muted"], "middle", ls=1)


# ============================================================================
#  Sáu màn. Toạ độ trong mỗi hàm chép NGUYÊN từ ui-screens.cpp —
#  nhớ trang nội dung bắt đầu ở y=24, nên y ở đây = y trong code + 24.
# ============================================================================
PY = 24   # CONTENT_Y


def home(offline=False):
    chrome(0, mqtt=not offline)
    # thẻ TRONG NHÀ  (card(p, 6, 2, 152, 84))
    card(6, PY + 2, 152, 84)
    text(16, PY + 10, "TRONG NHÀ", 12, C["muted"], ls=1)
    text(16, PY + 26, "26.8°C", 40, C["ok"], weight="bold")
    text(16, PY + 66, "ĐỘ ẨM", 12, C["muted"])
    text(106, PY + 64, "41%", 12)
    # thẻ NGOÀI TRỜI (card(p, 162, 2, 152, 84))
    card(162, PY + 2, 152, 84)
    text(172, PY + 10, "NGOÀI TRỜI", 12, C["muted"], ls=1)
    dot(296, PY + 11, C["err"] if offline else C["ok"])
    if offline:
        text(172, PY + 26, "--", 40, C["muted"], weight="bold")
        text(172, PY + 66, "MẤT NHỊP TIM 95s", 12, C["err"])
    else:
        text(172, PY + 26, "31.2°C", 40, C["warn"], weight="bold")
        text(172, PY + 66, "ĐỘ ẨM", 12, C["muted"])
        text(262, PY + 64, "68%", 12)
    # thẻ MÁY LẠNH  (card(p, 6, 92, 308, 86))
    card(6, PY + 92, 308, 86)
    out.append(f'<image x="191" y="{PY+126}" width="110" height="45" opacity="0.18" '
               f'href="data:image/png;base64,{AC_B64}"/>')
    text(18, PY + 102, "MÁY LẠNH", 16, C["text"], weight="bold")
    bg, lbl = (C["warn"], "GHI ĐÈ") if offline else (C["accent"], "TỰ ĐỘNG")
    rect(234, PY + 102, 68, 18, bg, 1.0, ch=4)
    text(268, PY + 105, lbl, 12, "#0A0E14" if offline else C["text"], "middle", ls=1)
    text(18, PY + 132, "LẠNH · QUẠT AUTO", 12, C["muted"])
    text(18, PY + 152, "lệnh cuối 3 phút trước" if not offline
         else "máy chủ sẽ giành lại quyền ở chu kỳ sau", 12, C["muted"])
    text(216, PY + 130, "24°C", 28, C["accentText"], weight="bold")


def control():
    chrome(1)
    out.append(f'<image x="26" y="{PY+27}" width="30" height="30" '
               f'href="data:image/png;base64,{MINUS_B64}"/>')
    rect(8, PY + 4, 68, 76, C["card"], BTN_OPA, C["bDefault"], 2, 4)
    out.append(f'<image x="27" y="{PY+27}" width="30" height="30" opacity="0.9" '
               f'href="data:image/png;base64,{MINUS_B64}"/>')
    card(84, PY + 4, 152, 76)
    text(160, PY + 12, "26", 40, C["accentText"], "middle", weight="bold")
    text(160, PY + 60, "GIỚI HẠN 16 - 30", 12, C["muted"], "middle")
    rect(244, PY + 4, 68, 76, C["card"], BTN_OPA, C["bDefault"], 2, 4)
    out.append(f'<image x="263" y="{PY+27}" width="30" height="30" opacity="0.9" '
               f'href="data:image/png;base64,{PLUS_B64}"/>')
    for i, (lbl, kind) in enumerate([("LẠNH", "selected"), ("KHÔ", "normal"),
                                     ("QUẠT", "off"), ("TẮT", "normal")]):
        button(6 + 78 * i, PY + 86, 74, 44, lbl, kind)
    button(6, PY + 136, 150, 42, "GỬI", "primary")
    button(164, PY + 136, 150, 42, "TỰ ĐỘNG")


def info():
    chrome(2)
    # Số liệu MINH HOẠ, không phải cấu hình thật. Cố ý dùng địa chỉ mẫu
    # (broker.example.com, MAC bịa) — mockup là ảnh công khai, không có lý do gì
    # in địa chỉ hạ tầng thật lên đó.
    rows = [("WIFI", "TEN_MANG_WIFI"), ("IP", "192.168.1.47"), ("SÓNG", "-58 dBm"),
            ("MQTT", "broker.example.com:1883"),
            ("ESP-NOW", "MAC A0:B7:65:2C:1D:44 · KÊNH 6"),
            ("NGOÀI TRỜI", "31.2°C · 68% · 4s trước"), ("MÃ IR", "LẠNH KHÔ TẮT"),
            ("FW", "1.4.0")]
    for i, (k, v) in enumerate(rows):
        y = PY + 4 + 20 * i
        text(12, y, k, 12, C["muted"], ls=1)
        text(308, y, v, 12, C["text"], "end")
        out.append(f'<line x1="12" y1="{y+17}" x2="308" y2="{y+17}" '
                   f'stroke="{C["bSubtle"]}" stroke-width="1"/>')
    text(12, PY + 166, "nhận 1842 · bỏ 3", 12, C["muted"])


def settings():
    chrome(3)
    for i, (lbl, y) in enumerate([("ĐỘ SÁNG", 4), ("ÂM BÁO", 50),
                                  ("ĐỒNG BỘ GIỜ", 96), ("KHỞI ĐỘNG LẠI", 142)]):
        card(6, PY + y, 308, 40)
        text(18, PY + y + 12, lbl, 16, C["text"], weight="bold")
    button(196, PY + 10, 28, 28, "-", size=12)
    text(230, PY + 16, "70%", 12, C["accentText"])
    button(268, PY + 10, 28, 28, "+", size=12)
    button(224, PY + 56, 36, 28, "BẬT", "primary")
    button(264, PY + 56, 36, 28, "TẮT")
    button(232, PY + 102, 68, 28, "CHẠY")
    rect(232, PY + 148, 68, 28, C["card"], BTN_OPA, C["err"], 2, 4)
    text(266, PY + 155, "CHẠY", 12, C["err"], "middle", ls=1)


def learn():
    chrome(1)
    rect(0, 0, W, H, "#000000", 0.55)
    card(16, 30, 288, 168)
    out.append(f'<polygon points="{chamfer_pts(16,30,288,168,6)}" fill="none" '
               f'stroke="{C["accent"]}" stroke-width="2"/>')
    text(160, 44, "ĐANG HỌC REMOTE", 12, C["accent"], "middle", ls=2)
    text(160, 78, "LẠNH", 40, C["text"], "middle", weight="bold")
    text(160, 130, "Hướng remote vào mắt thu, bấm nút", 12, C["muted"], "middle")
    rect(40, 154, 240, 10, C["bSubtle"], 1.0)
    rect(40, 154, 156, 10, C["accent"], 1.0)
    text(160, 170, "còn lại 19s", 12, C["warn"], "middle")


PANELS = [
    ("TRANG CHỦ — bình thường", "cả 2 node còn sống, máy chủ đang tự điều khiển",
     lambda: home(False)),
    ("TRANG CHỦ — mất node ngoài trời", "nhịp tim đứt 95s: chấm đỏ, số về '--', KHÔNG bịa giá trị",
     lambda: home(True)),
    ("ĐIỀU KHIỂN", "QUẠT mờ vì chưa học mã IR — vẫn thấy nút, chỉ là bấm không ăn",
     control),
    ("THÔNG TIN", "8 dòng chẩn đoán, nhãn trái / giá trị phải", info),
    ("CÀI ĐẶT", "KHỞI ĐỘNG LẠI viền đỏ — hành động phá huỷ tách khỏi phần còn lại",
     settings),
    ("HỌC REMOTE", "lớp phủ chặn thao tác khác, có đồng hồ đếm ngược", learn),
]


def b64(path, size=None):
    im = Image.open(path).convert("RGBA")
    if size:
        im = im.resize(size, Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return base64.b64encode(buf.getvalue()).decode()


A = os.path.join(HERE, "..", "..", "Interface", "LVGL", "assets")
BG_B64 = b64(BG)
AC_B64 = b64(os.path.join(A, "air-conditioning.png"))
PLUS_B64 = b64(os.path.join(A, "plus.png"))
MINUS_B64 = b64(os.path.join(A, "min_nimum.png"))

COLS, GAP, M, CAP = 2, 36, 40, 40
PW, PH = W * S, H * S
HEAD, FOOT = 130, 150
TOTAL_W = M * 2 + COLS * PW + (COLS - 1) * GAP
ROWS = (len(PANELS) + COLS - 1) // COLS
TOTAL_H = HEAD + ROWS * (CAP + PH + GAP) + FOOT

doc = [f'<svg xmlns="http://www.w3.org/2000/svg" '
       f'xmlns:xlink="http://www.w3.org/1999/xlink" '
       f'width="{TOTAL_W}" height="{TOTAL_H}" viewBox="0 0 {TOTAL_W} {TOTAL_H}">',
       f'<rect width="{TOTAL_W}" height="{TOTAL_H}" fill="#05080F"/>',
       f'<text x="{M}" y="52" font-family="Arial" font-size="30" font-weight="bold" '
       f'fill="{C["text"]}" letter-spacing="2">AIRCON — PANEL 320×240 (LVGL)</text>',
       f'<text x="{M}" y="80" font-family="Arial" font-size="15" fill="{C["muted"]}">'
       f'Hình học từ hệ thiết kế web admin (vát 45° hai góc, chữ hoa giãn) · '
       f'màu từ bảng CARBON của app · nền 12.png · thẻ kính mờ</text>',
       f'<text x="{M}" y="102" font-family="Arial" font-size="13" fill="#5A7186">'
       f'Sinh bằng tools/make_mockup.py — KHÔNG phải ảnh chụp màn hình thật. '
       f'Toạ độ chép từ src/ui/ui-screens.cpp.</text>']

for i, (title, note, fn) in enumerate(PANELS):
    cx = M + (i % COLS) * (PW + GAP)
    cy = HEAD + (i // COLS) * (CAP + PH + GAP)
    doc.append(f'<text x="{cx}" y="{cy+14}" font-family="Arial" font-size="16" '
               f'font-weight="bold" fill="{C["accentText"]}" letter-spacing="1">'
               f'{esc(str(i+1))}. {esc(title)}</text>')
    doc.append(f'<text x="{cx}" y="{cy+32}" font-family="Arial" font-size="12.5" '
               f'fill="{C["muted"]}">{esc(note)}</text>')
    out = []
    doc.append(f'<g transform="translate({cx},{cy+CAP}) scale({S})">')
    doc.append(f'<image x="0" y="0" width="{W}" height="{H}" '
               f'href="data:image/png;base64,{BG_B64}"/>')
    fn()
    doc.extend(out)
    doc.append("</g>")
    doc.append(f'<rect x="{cx}" y="{cy+CAP}" width="{PW}" height="{PH}" fill="none" '
               f'stroke="#31465C" stroke-width="1"/>')

# --- Dải màu ở chân trang ----------------------------------------------------
fy = HEAD + ROWS * (CAP + PH + GAP) + 12
doc.append(f'<text x="{M}" y="{fy}" font-family="Arial" font-size="15" '
           f'font-weight="bold" fill="{C["text"]}" letter-spacing="1">BẢNG MÀU</text>')
sw = [("nền", C["bg"]), ("thẻ", C["card"]), ("viền", C["bDefault"]),
      ("chữ", C["text"]), ("chữ mờ", C["muted"]), ("nhấn", C["accent"]),
      ("số nhấn", C["accentText"]), ("dễ chịu", C["ok"]), ("ấm", C["warn"]),
      ("nóng/lỗi", C["err"])]
for i, (nm, col) in enumerate(sw):
    x = M + i * 118
    doc.append(f'<rect x="{x}" y="{fy+14}" width="104" height="34" fill="{col}" '
               f'stroke="#31465C"/>')
    doc.append(f'<text x="{x}" y="{fy+64}" font-family="Arial" font-size="12" '
               f'fill="{C["muted"]}">{esc(nm)}</text>')
    doc.append(f'<text x="{x}" y="{fy+80}" font-family="Arial" font-size="11.5" '
               f'fill="#5A7186">{col.upper()}</text>')

doc.append(f'<text x="{M}" y="{fy+114}" font-family="Arial" font-size="12.5" '
           f'fill="#5A7186">Thang nhiệt: &lt;22°C nhấn · 22-27 dễ chịu · '
           f'27-32 ấm · &gt;32 nóng · không đo được thì XÁM (không bao giờ xanh — '
           f'xanh nói “phòng đang mát”, tức khẳng định điều ta không biết)</text>')
doc.append("</svg>")

with io.open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(doc))
print(f"{os.path.normpath(OUT)}  ({os.path.getsize(OUT)/1024:.0f} KB, "
       f"{TOTAL_W}x{TOTAL_H})")
