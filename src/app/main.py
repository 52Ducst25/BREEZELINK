"""FastAPI application factory.

Wires logging, middleware, exception handlers, the /api/v1 router, the SSR admin,
and the static mount. Exposed publicly through a Cloudflare tunnel at
admin.vi-du.com -- see docker/docker-compose.vps.yml.
"""

from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import RedirectResponse
from fastapi.staticfiles import StaticFiles
from slowapi import _rate_limit_exceeded_handler
from slowapi.errors import RateLimitExceeded

from app.api.v1.router import api_router
from app.config import get_settings
from app.core.exceptions import register_exception_handlers
from app.core.logging_config import configure_logging
from app.core.middleware import RequestContextMiddleware
from app.core.rate_limit import limiter

STATIC_DIR = Path(__file__).parent / "web" / "static"


def create_app() -> FastAPI:
    settings = get_settings()
    configure_logging(settings.log_level)

    app = FastAPI(
        title=settings.app_name,
        version="0.1.0",
        description="Edge-AI adaptive air-conditioning control (multi-tenant, 2-node outdoor/indoor).",
    )

    # Brute-force throttle on login/register. Keyed on CF-Connecting-IP: behind the
    # tunnel every request's client host is 127.0.0.1, so without that header the
    # whole internet shares one bucket and one attacker locks everybody out.
    app.state.limiter = limiter
    app.add_exception_handler(RateLimitExceeded, _rate_limit_exceeded_handler)

    app.add_middleware(RequestContextMiddleware)
    register_exception_handlers(app)

    # /api/v1/* -- Bearer auth (Flutter app) + the WebSocket live feed.
    app.include_router(api_router)

    # OTA lives OUTSIDE /api/v1 on purpose — see api/ota_routes for why the
    # update check must stay reachable by a build too old to speak the current
    # API. cloudflared shares the api container's network stack, so this is
    # served through the existing tunnel with no Cloudflare-side change.
    from app.api.ota_routes import router as ota_router

    app.include_router(ota_router)

    # SSR admin (/web/*) -- cookie auth. Registered lazily so a partially-built web
    # layer can't stop the API from booting during development.
    _register_web(app)

    app.mount("/web-static", StaticFiles(directory=str(STATIC_DIR)), name="web-static")

    @app.get("/", include_in_schema=False)
    async def root(_: Request) -> RedirectResponse:
        return RedirectResponse("/web", status_code=307)

    return app


def _register_web(app: FastAPI) -> None:
    """Attach the SSR routers + the auth redirect handler.

    An SSR page must never answer an expired session with a JSON 401 -- the web
    dependencies raise RedirectToLogin and this handler turns it into a 303.
    """
    from app.web.dependencies import RedirectToLogin
    from app.web.routes import router as web_router

    @app.exception_handler(RedirectToLogin)
    async def _to_login(_: Request, __: RedirectToLogin) -> RedirectResponse:
        return RedirectResponse("/web/login", status_code=303)

    app.include_router(web_router)

    from app.web.admin_routes import router as admin_router

    app.include_router(admin_router)

    from app.web.customer_routes import router as customer_router

    app.include_router(customer_router)

    from app.web.release_routes import router as release_router

    app.include_router(release_router)


app = create_app()
