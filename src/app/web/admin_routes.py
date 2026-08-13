"""Vendor staff accounts — who may sign in to this back office.

Every route depends on ``require_sysadmin`` (web/dependencies.py) instead of
repeating a role check inline — see that module's docstring for why
SafeKitchen's inline-``if`` pattern was worth fixing while porting.

Only STAFF accounts. Customers' accounts are created by the customers
themselves (auth_service.register, redeeming a code) and are listed under
their own customer page, not here.
"""

import uuid
from urllib.parse import quote

from fastapi import APIRouter, Depends, File, Form, Request, UploadFile
from fastapi.responses import RedirectResponse, Response
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.user import User, UserRole
from app.services import user_service
from app.web.dependencies import get_db, require_sysadmin, require_web_user
from app.web.templating import render

router = APIRouter(prefix="/web", tags=["ssr-admin"])

# Profile pictures are held in a Postgres column and re-sent on demand, so the
# ceiling is deliberately small — this is a 36px circle in the top bar, not a
# photo library. There is no Pillow in the image to downscale with, so the size
# cap IS the whole defence against someone storing a 20MB DSLR frame.
MAX_AVATAR_BYTES = 2 * 1024 * 1024

# Sniffed from the bytes rather than trusted from the upload's Content-Type,
# which is attacker-controlled: without this, "evil.html" renamed to .png would
# be stored and then served back from our own origin.
_IMAGE_MAGIC: tuple[tuple[bytes, str], ...] = (
    (b"\x89PNG\r\n\x1a\n", "image/png"),
    (b"\xff\xd8\xff", "image/jpeg"),
    (b"GIF87a", "image/gif"),
    (b"GIF89a", "image/gif"),
)


def _sniff_image(data: bytes) -> str | None:
    """Return the real image mime for these bytes, or None if unsupported."""
    for magic, mime in _IMAGE_MAGIC:
        if data.startswith(magic):
            return mime
    # WEBP is RIFF-framed: "RIFF" .... "WEBP"
    if len(data) >= 12 and data[0:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "image/webp"
    return None


# ============================== USERS =====================================

@router.get("/users")
async def users_page(
    request: Request,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Vendor staff accounts — the only accounts this web page manages.

    It used to list ``list_by_org(user.org_id)``: the members of whatever
    household the viewer belonged to. That was left over from when the admin
    was a household's own dashboard. Customers' accounts are not managed here
    at all — a customer creates their own by redeeming a code, and the codes
    are issued from the customer page.
    """
    return render(request, "users.html", {
        "user": user, "nav": "users",
        "users": await user_service.list_sysadmins(session),
        "err": request.query_params.get("err"),
    })


@router.post("/users")
async def create_user_submit(
    email: str = Form(...),
    full_name: str = Form(""),
    password: str = Form(...),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Add a colleague to the back office.

    ``is_sysadmin=True`` and ``role=owner`` are fixed, not form fields: this
    page exists only to create staff, and a "role" dropdown here would just be
    a way to create a half-broken account that can sign in and then be refused
    by every gate.

    The new account lands in the creator's org. That org is not a household —
    it is only where staff happen to live — and nothing about staff access is
    read off it (require_sysadmin checks the flag, not the org).
    """
    existing = await user_service.get_by_org_email(session, user.org_id, email)
    if existing is not None:
        return RedirectResponse(
            f"/web/users?err={quote('Email đã tồn tại')}", status_code=303
        )
    await user_service.create_user(
        session,
        org_id=user.org_id,
        email=email,
        password=password,
        role=UserRole.owner,
        full_name=full_name or None,
        is_sysadmin=True,
    )
    await session.commit()
    return RedirectResponse("/web/users", status_code=303)


async def _staff_or_none(session: AsyncSession, user_id: uuid.UUID) -> User | None:
    """Resolve a STAFF account by id, or None.

    Identified by the ``is_sysadmin`` flag, NOT by sharing the caller's org.
    The list on this page is cross-org (staff were seeded by hand and sit in
    whatever org they were made in), so an org check here would show a
    colleague in the table and then refuse to open them.

    The flag check is what stops this route being a back door into editing a
    customer's account — including resetting their password — by pasting their
    id into the URL.
    """
    target = await user_service.get_by_id(session, user_id)
    if target is None or not target.is_sysadmin:
        return None
    return target


@router.get("/users/{user_id}/edit")
async def edit_user_page(
    request: Request,
    user_id: uuid.UUID,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    target = await _staff_or_none(session, user_id)
    if target is None:
        return RedirectResponse("/web/users", status_code=303)
    return render(request, "edit_user.html", {"user": user, "nav": "users", "target": target})


@router.post("/users/{user_id}/edit")
async def edit_user_submit(
    user_id: uuid.UUID,
    full_name: str = Form(""),
    phone: str = Form(""),
    is_active: str = Form(""),
    password: str = Form(""),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Edit a staff account.

    No ``role`` field: role is authority inside a household, and staff are not
    a household. It decides nothing here (require_sysadmin reads the flag), so
    offering the dropdown would only let someone set a value that silently
    does nothing.
    """
    target = await _staff_or_none(session, user_id)
    if target is None:
        return RedirectResponse("/web/users", status_code=303)
    await user_service.update_user(
        session,
        target,
        full_name=full_name or None,
        phone=phone or None,
        is_active=bool(is_active),
        password=password or None,
    )
    await session.commit()
    return RedirectResponse("/web/users", status_code=303)


@router.post("/users/{user_id}/delete")
async def delete_user_submit(
    user_id: uuid.UUID,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    if user_id == user.id:
        return RedirectResponse(
            f"/web/users?err={quote('Không thể tự xoá chính mình')}", status_code=303
        )
    # Staff-only, same as the edit routes: without the flag check this would
    # delete any customer's account by id.
    target = await _staff_or_none(session, user_id)
    if target is None:
        return RedirectResponse("/web/users", status_code=303)
    await user_service.delete_user(session, target)
    # MUST commit: delete_user only flushes (same contract as create_user /
    # update_user — the route owns the transaction), and get_db closes the
    # session without committing, so without this the DELETE is rolled back and
    # the page redirects as if it worked while the account still signs in.
    await session.commit()
    return RedirectResponse("/web/users", status_code=303)


# The comfort-config pages used to live here, tuning ``user.org_id`` — the
# staff member's own org, which is not a household anyone lives in. They now
# hang off a customer (web/customer_routes: /web/customers/{org_id}/config),
# because "whose algorithm am I tuning" is only a meaningful question once you
# have named the customer.


# ============================== ACCOUNT ===================================
# The signed-in admin's OWN profile. Separate from /web/users (which manages
# colleagues) because "change my picture" needs no target id and must never be
# reachable for somebody else's account.


def _account_err(msg: str) -> RedirectResponse:
    return RedirectResponse(f"/web/account?err={quote(msg)}", status_code=303)


@router.get("/account")
async def account_page(request: Request, user: User = Depends(require_sysadmin)):
    return render(request, "account.html", {
        "user": user,
        "nav": "account",
        "err": request.query_params.get("err"),
        "ok": request.query_params.get("ok"),
    })


@router.get("/settings")
async def settings_page(request: Request, user: User = Depends(require_web_user)):
    """Tuỳ chọn hiển thị. KHÔNG phải trang quản trị.

    ``require_web_user`` chứ không phải ``require_sysadmin`` như phần còn lại của
    file này: chọn giao diện sáng hay tối là sở thích cá nhân, không phải quyền.
    Khoá nó sau quyền quản trị nghĩa là một nhân viên không phải sysadmin sẽ bị
    ép dùng giao diện sáng mãi mãi mà không có cách nào đổi.

    Trang này KHÔNG ghi gì xuống máy chủ. Lựa chọn nằm trong localStorage của
    trình duyệt, cùng chỗ với trạng thái gập/mở thanh bên — cùng lý do: nó phải
    có hiệu lực TRƯỚC lần vẽ đầu tiên, mà chờ máy chủ trả lời thì đã muộn và
    người dùng sẽ thấy giao diện sáng loé lên rồi mới chuyển tối.
    """
    return render(request, "settings.html", {"user": user, "nav": "settings"})


@router.post("/account/avatar")
async def upload_avatar_submit(
    avatar: UploadFile = File(...),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Replace the signed-in admin's profile picture.

    Always acts on ``user`` from the session — never on an id from the request —
    so there is no path here to overwrite a colleague's picture.
    """
    # BOUNDED read. An unbounded ``read()`` would materialise the whole part in
    # RAM before the cap below could reject it, so a signed-in admin could pin
    # arbitrary memory in the same container that serves the API and the live
    # feed. Reading cap+1 bytes is exactly enough to know it is oversize —
    # which also means len(data) is then always cap+1, so the message must not
    # quote it as if it were the file's real size.
    data = await avatar.read(MAX_AVATAR_BYTES + 1)
    if not data:
        return _account_err("Chưa chọn tệp ảnh")
    if len(data) > MAX_AVATAR_BYTES:
        return _account_err(f"Ảnh quá lớn. Tối đa {MAX_AVATAR_BYTES // 1024} KB.")
    mime = _sniff_image(data)
    if mime is None:
        return _account_err("Tệp không phải ảnh hợp lệ (chỉ nhận PNG, JPG, GIF, WEBP)")

    await user_service.set_avatar(session, user, data, mime)
    await session.commit()
    return RedirectResponse(f"/web/account?ok={quote('Đã cập nhật ảnh đại diện')}", status_code=303)


@router.post("/account/avatar/delete")
async def delete_avatar_submit(
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    await user_service.clear_avatar(session, user)
    await session.commit()
    return RedirectResponse(f"/web/account?ok={quote('Đã xoá ảnh đại diện')}", status_code=303)


@router.get("/users/{user_id}/avatar")
async def serve_avatar(
    user_id: uuid.UUID,
    _: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Serve one staff picture.

    By id (not "mine") so the same URL works for the top bar and for any list of
    colleagues. Behind the same sysadmin gate as every other page — a profile
    photo is not public. Cached hard because the URL carries an
    ``avatar_updated_at`` cache-buster: a new upload changes the URL, so a long
    max-age can never pin a stale picture.
    """
    found = await user_service.get_avatar(session, user_id)
    if found is None:
        return Response(status_code=404)
    data, mime, updated = found
    return Response(
        content=data,
        media_type=mime,
        headers={
            "Cache-Control": "private, max-age=86400",
            "ETag": f'"{int(updated.timestamp())}"',
        },
    )
