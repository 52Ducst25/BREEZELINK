"""SSR entry points: login/logout + the vendor's fleet dashboard.

Cookie auth (web/dependencies.py) — handlers call the exact same service layer
the JSON API uses, so the numbers shown here and the numbers the Flutter app
shows can never silently drift apart (one domain layer, two renderers).

This whole admin is vendor-only; customers use the app. Anything about one
customer (their nodes, readings, accounts, codes, comfort config) lives in
web/customer_routes, scoped by that customer's org rather than the viewer's.
"""

from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.ext.asyncio import AsyncSession

from app.config import get_settings
from app.core.exceptions import AppException
from app.core.security import SESSION_COOKIE_NAME, create_web_session_token
from app.models.user import User
from app.services import auth_service
from app.web import fleet_view
from app.web.dependencies import get_current_user_or_none, get_db, require_sysadmin
from app.web.templating import render

router = APIRouter(prefix="/web", tags=["ssr"])

# The session cookie is only forced Secure outside dev/test — same
# convenience trade config.py's own default-secret check already makes.
_INSECURE_COOKIE_ENVIRONMENTS = {"development", "test", "testing"}


def _cookie_secure() -> bool:
    return get_settings().environment.lower() not in _INSECURE_COOKIE_ENVIRONMENTS


@router.get("/login")
async def login_page(request: Request, session: AsyncSession = Depends(get_db)):
    current = await get_current_user_or_none(request, session)
    # Only bounce a session that can actually USE /web. Redirecting any valid
    # session would trap a customer holding a pre-lockdown cookie in a loop:
    # /web/login -> /web -> require_sysadmin says no -> /web/login -> ...
    # Showing them the form instead lets them log in as someone who may enter.
    if current is not None and current.is_sysadmin:
        return RedirectResponse("/web", status_code=303)
    return render(request, "login.html", {"user": None, "error": None})


@router.post("/login")
async def login_submit(
    request: Request,
    email: str = Form(...),
    password: str = Form(...),
    session: AsyncSession = Depends(get_db),
):
    try:
        user = await auth_service.login_user(session, email, password)
    except AppException:
        # Deliberately generic — mirrors the API's UnauthorizedError message,
        # never distinguishes "no such email" from "wrong password" (no
        # account enumeration via the login form).
        return render(request, "login.html", {"user": None, "error": "Email hoặc mật khẩu không đúng"})

    # This admin is vendor-only. Customers own valid credentials — they just
    # belong in the app — so the check has to be HERE, at the point a session
    # would be minted, not only on each page. Handing out a cookie we then
    # refuse on every route is what creates the /web/login <-> /web loop, and
    # it tells the customer nothing about what went wrong.
    #
    # Says what to do instead, and does NOT reuse the generic credentials
    # error: at this point they have already proven the password, so hiding
    # the reason protects nothing and just looks broken to a paying customer.
    if not user.is_sysadmin:
        return render(request, "login.html", {
            "user": None,
            "error": "Tài khoản này dành cho khách hàng — hãy dùng ứng dụng Aircon trên điện thoại. "
                     "Trang quản trị chỉ dành cho nhân viên.",
        })

    # Persist the last_login_at that login_user stamped (it only flushed).
    await session.commit()

    settings = get_settings()
    resp = RedirectResponse("/web", status_code=303)
    resp.set_cookie(
        SESSION_COOKIE_NAME,
        create_web_session_token(str(user.id), str(user.org_id), user.role.value),
        httponly=True,
        secure=_cookie_secure(),
        samesite="strict",
        max_age=settings.web_session_ttl_hours * 3600,
        path="/",
    )
    return resp


@router.post("/logout")
async def logout() -> RedirectResponse:
    resp = RedirectResponse("/web/login", status_code=303)
    resp.delete_cookie(SESSION_COOKIE_NAME, path="/")
    return resp


# ---- Password reset — PUBLIC. The reset link e-mailed to a customer opens
# here; the signed token IS the authorization, so there is deliberately no
# login gate (they forgot their password — of course they are not logged in).
# The same page serves every account, customer or staff.


@router.get("/reset-password")
async def reset_password_page(request: Request):
    """Render the new-password form. The token rides in the query string of the
    link (mailer builds ``{base}?token=…``) and is carried through in a hidden
    field to the POST — the page itself does not validate it, the submit does,
    so an expired link still shows the form rather than a bare error."""
    token = request.query_params.get("token", "")
    return render(request, "reset_password.html", {"user": None, "token": token, "error": None, "done": False})


@router.post("/reset-password")
async def reset_password_submit(
    request: Request,
    token: str = Form(...),
    new_password: str = Form(...),
    confirm_password: str = Form(""),
    session: AsyncSession = Depends(get_db),
):
    ctx = {"user": None, "token": token, "error": None, "done": False}
    if len(new_password) < 8:
        ctx["error"] = "Mật khẩu cần ít nhất 8 ký tự."
        return render(request, "reset_password.html", ctx)
    if new_password != confirm_password:
        ctx["error"] = "Hai mật khẩu không khớp."
        return render(request, "reset_password.html", ctx)
    try:
        await auth_service.reset_password(session, token, new_password)
    except AppException as exc:
        ctx["error"] = exc.detail
        return render(request, "reset_password.html", ctx)
    ctx["done"] = True
    return render(request, "reset_password.html", ctx)


@router.get("")
async def dashboard(
    request: Request,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Fleet view — every node the vendor has sold, across all customers.

    This used to render the viewer's OWN household (comfort hero, pipeline,
    indoor/outdoor tiles) off ``user.org_id``. Once the admin became
    vendor-only that was actively misleading: staff logged in and studied the
    comfort decision of a demo household nobody lives in, while the customers
    they actually sold to were invisible. The per-household view lives in the
    Flutter app, where the person who feels the room is.
    """
    return render(request, "dashboard.html", {
        "user": user,
        "nav": "dashboard",
        **await fleet_view.build(session),
    })


# /web/devices and /web/devices/{id} used to live here: the viewer's OWN
# household's nodes. Both are gone. Vendor staff have no household to inspect,
# and every node they care about belongs to a customer — reached through
# /web/customers/{org}/devices/{id}, which scopes by the customer's org rather
# than the viewer's. The fleet dashboard above links straight there.
