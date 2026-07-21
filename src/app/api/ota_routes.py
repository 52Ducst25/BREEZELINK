"""Public OTA endpoints: the update manifest + the APK download.

Deliberately UNAUTHENTICATED, and mounted outside /api/v1. The app checks for
updates on launch — before anyone has logged in — and a build old enough to be
force-updated may not even speak the current auth contract. Gating this behind
a token would make the update mechanism unusable exactly when it matters most:
shipping a fix to a broken build.

Nothing secret is exposed: the manifest is version metadata, and the APK is the
same file we hand to every customer. The download is rate-limited per IP
anyway, because it is a ~50MB file and an open one.
"""

from fastapi import APIRouter, Depends, Request
from fastapi.responses import FileResponse
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.database import get_db
from app.core.rate_limit import limiter
from app.services import release_service

router = APIRouter(prefix="/app", tags=["ota"])


@router.get("/update.json")
@limiter.limit("30/minute")
async def update_manifest(request: Request, session: AsyncSession = Depends(get_db)) -> dict:
    """What the app polls to decide whether it is out of date.

    Returns a well-formed "nothing published" answer instead of 404: a 404 is
    indistinguishable from a broken server, and the app would nag the user
    about a failed update check on a system that simply has no release yet.
    """
    manifest = await release_service.build_manifest(session)
    if manifest is None:
        return {
            "latestVersionCode": 0,
            "latestVersionName": "",
            "apkUrl": "",
            "changelog": "",
            "minSupportedVersionCode": 1,
            "apkSize": 0,
        }
    return manifest


@router.get("/download/{filename}")
# Tighter than the manifest's: this is ~50MB a hit and open to the internet.
# Raised 10 -> 20/hour: the app now auto-retries a stalled download up to 3x,
# so a single "Cập nhật" tap can spend 3 of these. At 10 the customer hit 429
# after ~3 taps and the app reported it as a network failure.
# The limiter keys on CF-Connecting-IP (core/rate_limit) — behind the tunnel
# every client looks like 127.0.0.1 and would otherwise share one bucket.
@limiter.limit("20/hour")
async def download_apk(filename: str, request: Request) -> FileResponse:
    """Stream a published APK.

    ``resolve_apk`` is what makes this safe to expose: the name must match the
    exact pattern release_service itself mints, and the resolved path must
    still sit inside the releases dir. Without both, ``filename`` is a
    path-traversal hole straight into the container.
    """
    path = release_service.resolve_apk(filename)  # raises NotFoundError -> 404
    return FileResponse(
        path,
        # Android will not offer to install it without this exact type; served
        # as octet-stream the browser just saves a file the user cannot open.
        media_type="application/vnd.android.package-archive",
        filename=filename,
    )
