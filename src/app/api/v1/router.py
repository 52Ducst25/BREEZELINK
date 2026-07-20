"""Aggregate API v1 router. Domain routers are included here.

Phase 5 adds device/comfort/ir/config/telemetry routers (see
plan phase-05-api-endpoints.md). All routers use static path segments only
(no ``/{id}`` path params in this API), so registration order does not
affect route matching — kept alphabetical-ish by domain for readability.
"""

from fastapi import APIRouter, Depends

from app.api.deps import require_role
from app.api.v1 import (
    auth_routes,
    comfort_routes,
    config_routes,
    device_routes,
    energy_routes,
    ir_routes,
    telemetry_routes,
    ws_routes,
)
from app.models.user import User

api_router = APIRouter(prefix="/api/v1")


@api_router.get("/health", tags=["health"])
async def health() -> dict[str, str]:
    """Liveness probe."""
    return {"status": "ok"}


@api_router.get("/admin/ping", tags=["admin"])
async def admin_ping(user: User = Depends(require_role("owner"))) -> dict[str, str]:
    """Stub admin-only route — smoke-tests the require_role() guard factory."""
    return {"status": "ok", "org_id": str(user.org_id)}


api_router.include_router(auth_routes.router)
api_router.include_router(device_routes.router)
api_router.include_router(comfort_routes.router)
api_router.include_router(ir_routes.router)
api_router.include_router(config_routes.router)
api_router.include_router(telemetry_routes.router)
api_router.include_router(energy_routes.router)
api_router.include_router(ws_routes.router)  # WebSocket /ws/live realtime feed
