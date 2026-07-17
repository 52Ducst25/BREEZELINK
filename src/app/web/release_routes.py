"""Vendor-only SSR pages: publishing the customer app (OTA).

Upload an APK, it becomes the current release, and every customer's app offers
it on next launch. Rolling back is re-pointing ``is_current`` at an older row —
the APK files stay on the volume, so a bad build is undone in one click rather
than by rebuilding and re-uploading.

The public half (manifest + download) lives in api/ota_routes, unauthenticated
by necessity.
"""

import uuid

from fastapi import APIRouter, Depends, File, Form, Request, UploadFile
from fastapi.responses import RedirectResponse
from sqlalchemy.ext.asyncio import AsyncSession
from urllib.parse import quote

from app.core.exceptions import AppException
from app.models.user import User
from app.services import release_service
from app.web.dependencies import get_db, require_sysadmin
from app.web.templating import render

router = APIRouter(prefix="/web", tags=["ssr-releases"])


def _err(msg: str) -> RedirectResponse:
    return RedirectResponse(f"/web/releases?err={quote(msg)}", status_code=303)


@router.get("/releases")
async def releases_page(
    request: Request,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    return render(
        request,
        "releases.html",
        {
            "user": user,
            "nav": "releases",
            "releases": await release_service.list_releases(session),
            "current": await release_service.get_current(session),
            "err": request.query_params.get("err"),
        },
    )


@router.post("/releases")
async def publish_release_submit(
    version_code: int = Form(...),
    version_name: str = Form(...),
    changelog: str = Form(""),
    min_supported_code: int = Form(1),
    apk: UploadFile = File(...),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Upload an APK and make it the current release.

    The file is read fully into memory before it is handed to the service. That
    is fine at this size (a Flutter APK is ~50MB, capped at 300MB by
    release_service.MAX_APK_BYTES) and it buys the checks that matter: the
    duplicate-version and ZIP-magic guards run BEFORE anything touches the
    volume, so a rejected upload leaves no orphan file behind.
    """
    if not version_name.strip():
        return _err("Tên phiên bản không được để trống")
    if version_code < 1:
        return _err("Version code phải là số nguyên dương")

    data = await apk.read()
    try:
        await release_service.publish(
            session,
            version_code=version_code,
            version_name=version_name,
            data=data,
            changelog=changelog.strip() or None,
            min_supported_code=min_supported_code,
            published_by=user.id,
        )
    except AppException as exc:
        return _err(exc.detail)
    await session.commit()
    return RedirectResponse("/web/releases", status_code=303)


@router.post("/releases/{release_id}/current")
async def set_current_submit(
    release_id: uuid.UUID,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Roll back (or forward) to an already-uploaded build."""
    try:
        await release_service.set_current(session, release_id)
    except AppException as exc:
        return _err(exc.detail)
    await session.commit()
    return RedirectResponse("/web/releases", status_code=303)


@router.post("/releases/{release_id}/delete")
async def delete_release_submit(
    release_id: uuid.UUID,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Remove a build entirely — row and APK.

    Refuses the current one: deleting what every app is being told to download
    would leave the manifest advertising a 404. Point ``is_current`` elsewhere
    first, which is a deliberate two-step.
    """
    current = await release_service.get_current(session)
    if current is not None and current.id == release_id:
        return _err("Không thể xoá bản đang phát hành — hãy chọn bản khác làm bản phát hành trước")
    try:
        await release_service.delete_release(session, release_id)
    except AppException as exc:
        return _err(exc.detail)
    await session.commit()
    return RedirectResponse("/web/releases", status_code=303)
