"""Jinja2 template environment for the SSR admin.

Directory resolved from this file's own location (not the process CWD) so
the app renders correctly whether started from the repo root, the ``src``
package dir, or inside the container image.
"""

from pathlib import Path

from fastapi import Request
from fastapi.templating import Jinja2Templates

TEMPLATE_DIR = Path(__file__).resolve().parent / "templates"

# Bump on any app.css/app.js change. ds/* is the vendored design system,
# linked WITHOUT "?v=" (see base.html's comment on that link) -- editing it
# needs a hard refresh, not a version bump here.
ASSET_VERSION = "260717-16"


def _format_datetime(ts) -> str:
    """Local wall-clock time for a tz-aware datetime; em-dash when absent.

    Mirrors the project-wide rule that missing data renders as "—", never as
    a fabricated/blank value (see comfort_preview_service's docstring on the
    bug this guards against).
    """
    if ts is None:
        return "—"
    return ts.astimezone().strftime("%d/%m/%Y %H:%M:%S")


def _format_number(value, digits: int = 1) -> str:
    """Fixed-point number, or an em-dash for ``None`` -- used everywhere a
    comfort-pipeline value may be legitimately absent (see comfort.py schema).
    """
    if value is None:
        return "—"
    return f"{value:.{digits}f}"


templates = Jinja2Templates(directory=str(TEMPLATE_DIR))
templates.env.filters["datetime"] = _format_datetime
templates.env.filters["num"] = _format_number


def render(request: Request, name: str, context: dict | None = None):
    """``TemplateResponse`` with the cache-bust version merged in (DRY --
    every route would otherwise repeat ``"v": ASSET_VERSION``).
    """
    data: dict = {"v": ASSET_VERSION}
    data.update(context or {})
    return templates.TemplateResponse(request, name, data)
