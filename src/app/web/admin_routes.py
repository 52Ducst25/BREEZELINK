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

from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.ext.asyncio import AsyncSession

from app.models.user import User, UserRole
from app.services import user_service
from app.web.dependencies import get_db, require_sysadmin
from app.web.templating import render

router = APIRouter(prefix="/web", tags=["ssr-admin"])


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
    return RedirectResponse("/web/users", status_code=303)


# The comfort-config pages used to live here, tuning ``user.org_id`` — the
# staff member's own org, which is not a household anyone lives in. They now
# hang off a customer (web/customer_routes: /web/customers/{org_id}/config),
# because "whose algorithm am I tuning" is only a meaningful question once you
# have named the customer.
