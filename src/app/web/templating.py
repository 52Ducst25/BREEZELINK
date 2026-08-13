"""Jinja2 template environment for the SSR admin.

Directory resolved from this file's own location (not the process CWD) so
the app renders correctly whether started from the repo root, the ``src``
package dir, or inside the container image.
"""

import hashlib
from datetime import datetime, timedelta, timezone
from pathlib import Path

from fastapi import Request
from fastapi.templating import Jinja2Templates

from app.services import device_presence

TEMPLATE_DIR = Path(__file__).resolve().parent / "templates"

def _asset_version() -> str:
    """Chuỗi phá đệm cho ``?v=``, tính từ CHÍNH NỘI DUNG của app.css + app.js.

    TRƯỚC ĐÂY LÀ MỘT HẰNG SỐ GÕ TAY, và nó hỏng đúng theo cách mà mọi thứ phải
    nhớ tay đều hỏng: sửa app.js, deploy, máy chủ có mã mới, nhưng trình duyệt
    vẫn chạy bản cũ thêm 4 tiếng nữa (``max-age=14400``) vì ``?v=`` không đổi.
    Không có lỗi nào hiện ra — chỉ là bản sửa "không có tác dụng". Đã dính đúng
    một lần: bản vá giữ trạng thái gập/mở deploy xong mà trang vẫn gập như cũ.

    BĂM NỘI DUNG CHỨ KHÔNG LẤY THỜI GIAN SỬA FILE. ``mtime`` nghe đơn giản hơn
    nhưng nó đi qua ba tầng đều có thể thay đổi nó — ``tar`` lúc đóng gói,
    ``COPY`` của Docker, và bản thân git (git KHÔNG lưu mtime, nên mỗi lần clone
    là một mốc thời gian mới). Băm nội dung thì chỉ đổi khi file thật sự đổi, và
    cho ra cùng một chuỗi trên mọi máy.

    Chi phí: đọc hai file ~20 KB đúng một lần lúc khởi động tiến trình.

    ds/* là bộ thiết kế nhập ngoài, cố ý KHÔNG gắn ``?v=`` (xem chú thích ở
    base.html) — sửa nó vẫn phải làm mới cứng, không liên quan tới hàm này.
    """
    # MỌI FILE ĐƯỢC NHÚNG KÈM "?v=", không chỉ css/js. Bản đầu chỉ băm app.css +
    # app.js, và nó hụt ngay lần dùng đầu tiên: đổi bộ favicon mà không đụng tới
    # hai file kia thì chuỗi băm không đổi, nên trình duyệt giữ icon CŨ thêm 4
    # tiếng — đúng thứ hàm này sinh ra để tránh.
    #
    # Danh sách này phải khớp với base.html. Thêm một file có "?v=" ở đó mà quên
    # ở đây thì file đó không bao giờ được làm mới.
    digest = hashlib.sha256()
    static_dir = Path(__file__).resolve().parent / "static"
    for name in ("app.css", "app.js", "favicon.ico", "favicon-32.png", "favicon-180.png"):
        path = static_dir / name
        if path.is_file():
            digest.update(path.read_bytes())
    # 10 ký tự hex = 40 bit. Đây là chuỗi phá đệm, không phải chữ ký bảo mật: nó
    # chỉ cần khác đi khi nội dung khác đi.
    return digest.hexdigest()[:10]


ASSET_VERSION = _asset_version()

# Vietnam time. The container runs UTC, so ``astimezone()`` was rendering every
# date 7 hours behind what a Vietnamese admin expects — the "sai ngày tạo" bug.
# Vietnam has no DST, so a fixed +7 is exact year-round.
_VN_TZ = timezone(timedelta(hours=7))


def _format_datetime(ts) -> str:
    """A tz-aware datetime as Vietnam wall-clock time; em-dash when absent.

    Mirrors the project-wide rule that missing data renders as "—", never as
    a fabricated/blank value (see comfort_preview_service's docstring on the
    bug this guards against).
    """
    if ts is None:
        return "—"
    return ts.astimezone(_VN_TZ).strftime("%d/%m/%Y %H:%M:%S")


def _format_ago(ts) -> str:
    """Relative "cách đây ..." for a past timestamp, or "chưa từng" if absent.

    Coarse on purpose (a vendor glancing at a staff list wants "3 giờ trước",
    not "3h 12m 4s"). Guards against a future timestamp (clock skew) by showing
    "vừa xong" rather than a negative duration.
    """
    if ts is None:
        return "chưa từng"
    now = datetime.now(timezone.utc)
    delta = now - ts.astimezone(timezone.utc)
    secs = int(delta.total_seconds())
    if secs < 0:
        return "vừa xong"
    if secs < 60:
        return "vừa xong"
    mins = secs // 60
    if mins < 60:
        return f"{mins} phút trước"
    hours = mins // 60
    if hours < 24:
        return f"{hours} giờ trước"
    days = hours // 24
    if days < 30:
        return f"{days} ngày trước"
    months = days // 30
    if months < 12:
        return f"{months} tháng trước"
    return f"{days // 365} năm trước"


def _is_online(ts) -> bool:
    """True if this last-seen timestamp is within the online window (2 min).

    Two minutes because the SSR admin auto-refreshes every ~5s and last_seen is
    written at most once a minute (web/dependencies) — so an open panel updates
    it well inside two minutes, and closing the panel drops "online" within two.
    """
    if ts is None:
        return False
    return (datetime.now(timezone.utc) - ts.astimezone(timezone.utc)).total_seconds() < 120


def _initials(user) -> str:
    """Two-letter monogram for the avatar fallback (DS .header-user-avatar).

    A filter rather than inline Jinja because the same monogram is rendered in
    the top bar on every page AND on the account page — the branching
    (full name -> first+last initial, one word -> first two letters, no name ->
    email) is too much to repeat correctly in two templates.
    """
    name = (getattr(user, "full_name", None) or "").strip()
    if name:
        parts = name.split()
        if len(parts) >= 2:
            return (parts[0][0] + parts[-1][0]).upper()
        return parts[0][:2].upper()
    email = (getattr(user, "email", "") or "?").strip()
    return email[:2].upper()


def _format_mac(value) -> str:
    """A 48-bit MAC stored as a BIGINT rendered as ``AA:BB:CC:DD:EE:FF``; em-dash
    when the node hasn't reported one yet (mac is null until the first reading).
    Used on the provisioning panel so a slave can be ESP-NOW-paired to its master.
    """
    if value is None:
        return "—"
    h = f"{int(value):012X}"
    return ":".join(h[i : i + 2] for i in range(0, 12, 2))


def _format_number(value, digits: int = 1) -> str:
    """Fixed-point number, or an em-dash for ``None`` -- used everywhere a
    comfort-pipeline value may be legitimately absent (see comfort.py schema).
    """
    if value is None:
        return "—"
    return f"{value:.{digits}f}"


def _format_field(value) -> str:
    """Giá trị cho ô ``<input type="number">``, đã gỡ nhiễu của float32.

    VẤN ĐỀ: bảng ``configs`` dùng kiểu ``real`` của Postgres — số thực 4 byte.
    Admin gõ 0.2, Postgres cất được số float32 gần nhất, và khi đọc ra Python
    nới nó thành float64 thì con số thật hiện nguyên hình::

        0.2   -> 0.20000000298023224
        0.8   -> 0.800000011920929
        0.05  -> 0.05000000074505806

    Chuỗi đó không chỉ xấu — nó làm ô nhập BỊ TỪ CHỐI. Ô ``ema_alpha`` khai
    ``step="0.01"``, mà 0.20000000298023224 không nằm trên lưới bước đó, nên
    trình duyệt chặn ngay khi bấm lưu:

        Vui lòng nhập giá trị hợp lệ. Hai giá trị hợp lệ gần nhất là 0,2 và 0,21.

    Tức là trang tự điền vào một giá trị mà chính nó không chấp nhận, và admin
    không sửa được cấu hình cho tới khi gõ đè tay từng ô.

    VÌ SAO ``%.7g``: float32 giữ khoảng 7 chữ số có nghĩa. Làm tròn về đúng 7 chữ
    số sẽ khôi phục lại con số thập phân mà người ta ĐÃ GÕ (mọi giá trị ở đây đều
    dưới 7 chữ số), mà không bịa thêm độ chính xác không có thật. Làm tròn cứng
    theo số chữ số thập phân thì sai: mỗi ô có ``step`` khác nhau (0.01, 0.1, 1).

    KHÔNG SỬA KIỂU CỘT THÀNH ``double precision``: nó không sai — 4 byte là quá
    đủ cho một hệ số 0..1, và một lần migration trên bảng cấu hình chỉ để làm đẹp
    chuỗi hiển thị là đổi lấy rủi ro không cần thiết. Nhiễu này thuộc về tầng
    hiển thị, nên sửa ở tầng hiển thị.
    """
    if value is None:
        return ""
    if isinstance(value, bool) or isinstance(value, int):
        return str(value)
    return f"{float(value):.7g}"


_NODE_TYPE_LABELS = {
    "outdoor": "Nút ngoài trời",
    "indoor": "Gateway trong nhà",
    "room": "Cảm biến trong phòng",
}


def _node_type_label(node_type) -> str:
    """Vietnamese name of a ``NodeType``, for every page that shows a node.

    A filter rather than the inline ``'A' if x == 'outdoor' else 'B'`` this
    replaces: that shape silently mislabels anything that is not the two kinds
    it knew about, so adding the room sensors would have shown four corner
    nodes as "Nút trong nhà" on three separate pages with nothing failing.
    """
    value = getattr(node_type, "value", node_type)
    return _NODE_TYPE_LABELS.get(str(value), str(value))


templates = Jinja2Templates(directory=str(TEMPLATE_DIR))
templates.env.filters["node_type"] = _node_type_label
templates.env.filters["datetime"] = _format_datetime
templates.env.filters["ago"] = _format_ago
templates.env.filters["online"] = _is_online
#  Bộ lọc RIÊNG cho THIẾT BỊ — đừng dùng "online" ở trên cho node.
#
#  Hai thứ khác nhau: "online" nhận một MỐC THỜI GIAN (lần đăng nhập cuối của
#  người dùng), còn cái này nhận cả ĐỐI TƯỢNG Device vì luật của thiết bị cần
#  cả cờ status lẫn độ tươi. Trộn hai cái là một trong hai chỗ sẽ im lặng cho
#  kết quả sai — xem services/device_presence.py.
templates.env.filters["device_online"] = device_presence.is_online
templates.env.filters["initials"] = _initials
templates.env.filters["mac"] = _format_mac
templates.env.filters["num"] = _format_number
templates.env.filters["field"] = _format_field


def render(request: Request, name: str, context: dict | None = None):
    """``TemplateResponse`` with the cache-bust version merged in (DRY --
    every route would otherwise repeat ``"v": ASSET_VERSION``).
    """
    data: dict = {"v": ASSET_VERSION}
    data.update(context or {})
    return templates.TemplateResponse(request, name, data)
