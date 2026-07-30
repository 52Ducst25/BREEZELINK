"""Publishing and serving app releases (OTA).

The vendor uploads an APK from the browser; it lands on a Docker named volume
(``/data/apk``, see docker/docker-compose*.yml) and a row records the metadata.
The customer's app polls ``/app/update.json`` and compares version codes.

Why the file lives on a volume and not in the image: the image is rebuilt on
every deploy. A release row would survive in Postgres while its APK silently
vanished, so the app would see an update advertised and 404 downloading it.

Ported from SafeKitchen's release_service with one change: SafeKitchen requires
the APK to be scp'd onto the host first and only registers what it finds on
disk (``unregistered_apks`` drives a dropdown). Here publishing IS the upload —
the person shipping a build has a web login, not necessarily a shell on the VPS.
"""

import logging
import re
import uuid
from pathlib import Path

from sqlalchemy import select, update
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.exceptions import ConflictError, NotFoundError
from app.models.app_release import AppRelease

logger = logging.getLogger("breezelink.releases")

# Matches the container path in docker/Dockerfile (chowned to appuser there) and
# the mount target in both compose files. Not derived from __file__ like
# SafeKitchen's: this is a volume mount point, unrelated to where the code sits.
RELEASES_DIR = Path("/data/apk")

# Upload guard. A real Flutter APK is ~50MB; 300 is generous headroom while
# still refusing someone streaming a DVD into the volume.
MAX_APK_BYTES = 300 * 1024 * 1024

# Whatever the browser reports as the filename is attacker-controlled. Rather
# than sanitising it (endless "../.." games), the name is REBUILT server-side
# from the version code and only matched against this.
#  Nhận CẢ HAI tiền tố. `aircon-` là tên cũ, trước khi dự án đổi lại thành
#  BreezeLink — và tên file đã được LƯU vào cột apk_filename của từng bản phát
#  hành, nên các bản cũ vẫn xin tải bằng đúng tên đó. Chỉ nhận `breezelink-` là
#  mọi bản đã phát hành trước đây lập tức 404 dù tệp vẫn nằm trong volume.
_SAFE_NAME = re.compile(r"^(?:aircon|breezelink)-[0-9]+-[0-9A-Za-z._-]+\.apk$")


def _filename_for(version_code: int, version_name: str) -> str:
    """Deterministic, server-built APK filename.

    Never trust the uploaded name — it decides a path we later write to and
    serve from. Version name is squeezed to a safe alphabet rather than
    escaped, because the only thing it needs to do here is be recognisable to
    a human reading the file list.
    """
    safe_version = re.sub(r"[^0-9A-Za-z._-]", "-", version_name.strip()) or "build"
    return f"breezelink-{version_code}-{safe_version}.apk"


def resolve_apk(filename: str) -> Path:
    """Map a download request's filename onto a real file, or raise.

    Two independent guards, because this is reachable unauthenticated:
      1. the name must match the pattern this service itself generates, which
         admits no separators and no "..";
      2. the resolved path must still be inside RELEASES_DIR — belt and braces
         against a pattern bug or a symlink planted in the volume.
    """
    if not _SAFE_NAME.match(filename):
        raise NotFoundError("Không tìm thấy tệp")
    path = (RELEASES_DIR / filename).resolve()
    if RELEASES_DIR.resolve() not in path.parents or not path.is_file():
        raise NotFoundError("Không tìm thấy tệp")
    return path


async def list_releases(session: AsyncSession, limit: int = 50) -> list[AppRelease]:
    """Newest build first — version_code, never created_at.

    A re-published older build (rollback) is created later but is NOT newer;
    ordering by time would put it on top and misrepresent the version history.
    """
    stmt = select(AppRelease).order_by(AppRelease.version_code.desc()).limit(limit)
    return list((await session.execute(stmt)).scalars().all())


async def get_current(session: AsyncSession) -> AppRelease | None:
    """The build being served, or None when nothing is published yet.

    Falls back to the highest version_code if no row carries the flag, so a
    database that lost the flag still serves something sensible instead of
    telling every app "no updates exist".
    """
    row = await session.execute(select(AppRelease).where(AppRelease.is_current.is_(True)))
    current = row.scalar_one_or_none()
    if current is not None:
        return current
    row = await session.execute(select(AppRelease).order_by(AppRelease.version_code.desc()).limit(1))
    return row.scalar_one_or_none()


async def _clear_current(session: AsyncSession) -> None:
    await session.execute(
        update(AppRelease).where(AppRelease.is_current.is_(True)).values(is_current=False)
    )


async def publish(
    session: AsyncSession,
    *,
    version_code: int,
    version_name: str,
    data: bytes,
    changelog: str | None = None,
    min_supported_code: int = 1,
    published_by: uuid.UUID | None = None,
) -> AppRelease:
    """Store the uploaded APK and make it the current release. Caller commits.

    The duplicate check comes BEFORE the file is written: publishing a code
    that already exists is a mistake, and writing the bytes first would leave
    an orphan file on the volume that nothing references and nobody cleans up.
    """
    existing = await session.execute(
        select(AppRelease).where(AppRelease.version_code == version_code)
    )
    if existing.scalar_one_or_none() is not None:
        raise ConflictError(f"Version code {version_code} đã được phát hành")

    if not data:
        raise ConflictError("Tệp APK rỗng")
    if len(data) > MAX_APK_BYTES:
        raise ConflictError(f"Tệp quá lớn (tối đa {MAX_APK_BYTES // 1024 // 1024} MB)")
    # An APK is a ZIP. Cheap sanity check that someone did not upload a PDF and
    # strand every customer's update on a file Android refuses to install.
    if data[:2] != b"PK":
        raise ConflictError("Tệp không phải APK hợp lệ")

    filename = _filename_for(version_code, version_name)
    RELEASES_DIR.mkdir(parents=True, exist_ok=True)
    path = RELEASES_DIR / filename
    path.write_bytes(data)
    logger.info("published apk %s (%d bytes)", filename, len(data))

    await _clear_current(session)
    release = AppRelease(
        version_code=version_code,
        version_name=version_name.strip(),
        apk_filename=filename,
        apk_size=len(data),  # measured, never client-reported
        changelog=changelog,
        min_supported_code=min_supported_code,
        is_current=True,
        published_by=published_by,
    )
    session.add(release)
    await session.flush()
    return release


async def set_current(session: AsyncSession, release_id: uuid.UUID) -> AppRelease:
    """Roll forward or back to an already-published build. Caller commits."""
    release = await session.get(AppRelease, release_id)
    if release is None:
        raise NotFoundError("Không tìm thấy bản phát hành")
    await _clear_current(session)
    release.is_current = True
    await session.flush()
    return release


async def delete_release(session: AsyncSession, release_id: uuid.UUID) -> None:
    """Remove a build and its APK. Caller commits.

    The row goes even if the file is already missing: a half-deleted release
    that keeps appearing in the list is worse than a tidy removal.
    """
    release = await session.get(AppRelease, release_id)
    if release is None:
        raise NotFoundError("Không tìm thấy bản phát hành")
    try:
        (RELEASES_DIR / release.apk_filename).unlink(missing_ok=True)
    except OSError as exc:
        logger.warning("could not unlink %s: %s", release.apk_filename, exc)
    await session.delete(release)
    await session.flush()


async def build_manifest(session: AsyncSession) -> dict | None:
    """The public update descriptor the app polls, or None if nothing is out.

    ``apkUrl`` is deliberately RELATIVE: the app already knows its own server
    (the user typed it at login) and baking an absolute host here would break
    every install the day the domain changes.
    """
    current = await get_current(session)
    if current is None:
        return None
    return {
        "latestVersionCode": current.version_code,
        "latestVersionName": current.version_name,
        "apkUrl": f"/app/download/{current.apk_filename}",
        "changelog": current.changelog or "",
        "minSupportedVersionCode": current.min_supported_code,
        "apkSize": current.apk_size,
    }
