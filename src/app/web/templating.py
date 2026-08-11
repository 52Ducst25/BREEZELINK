"""Jinja2 template environment for the SSR admin.

Directory resolved from this file's own location (not the process CWD) so
the app renders correctly whether started from the repo root, the ``src``
package dir, or inside the container image.
"""

from datetime import datetime, timedelta, timezone
from pathlib import Path

from fastapi import Request
from fastapi.templating import Jinja2Templates

TEMPLATE_DIR = Path(__file__).resolve().parent / "templates"

# Bump on any app.css/app.js change. ds/* is the vendored design system,
# linked WITHOUT "?v=" (see base.html's comment on that link) -- editing it
# needs a hard refresh, not a version bump here.
ASSET_VERSION = "260721-1"

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
templates.env.filters["initials"] = _initials
templates.env.filters["mac"] = _format_mac
templates.env.filters["num"] = _format_number


def render(request: Request, name: str, context: dict | None = None):
    """``TemplateResponse`` with the cache-bust version merged in (DRY --
    every route would otherwise repeat ``"v": ASSET_VERSION``).
    """
    data: dict = {"v": ASSET_VERSION}
    data.update(context or {})
    return templates.TemplateResponse(request, name, data)
