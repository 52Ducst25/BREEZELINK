"""Vendor-only SSR pages: customers, their nodes, and activation codes.

The shape of the job, mirroring SafeKitchen's "sell a device" flow: the vendor
records a sale (customer + however many nodes they bought), the system prints
codes, the buyer types one into the app and lands inside the org created here.

Every route depends on ``require_sysadmin`` — these pages read across
organizations, which nothing else in this admin is allowed to do.

Org-scoping note: handlers resolve a node via ``device_service.get_device(
session, org_id, device_id)`` with the org_id from the URL, never by device_id
alone. A bare ``session.get(Device, id)`` would happily edit a node belonging
to a customer whose URL you did not visit.
"""

import uuid
from urllib.parse import quote

from fastapi import APIRouter, Depends, Form, Request
from fastapi.responses import RedirectResponse
from sqlalchemy.ext.asyncio import AsyncSession

from app.core.exceptions import AppException
from app.models.enums import NodeRole, NodeType
from app.models.organization import OrgType
from app.models.user import User, UserRole
from app.schemas.config import ConfigUpdate
from app.services import (
    config_service,
    device_service,
    invite_service,
    live_events,
    organization_service,
    user_service,
)
from app.web import customer_view, device_view
from app.web.dependencies import get_db, require_sysadmin
from app.web.templating import render

router = APIRouter(prefix="/web", tags=["ssr-vendor"])

# Codes are read out over the phone; a slip of the finger should not create 500
# of them. Same ceiling SafeKitchen put on its own "số mã" field.
_MAX_CODES = 20
# A household is an outdoor node plus indoor nodes; double digits already means
# a data-entry mistake, not a real sale.
_MAX_NODES = 20


def _err(msg: str, path: str = "/web/customers") -> RedirectResponse:
    """Post/Redirect/Get with the message in the query string.

    Same pattern as admin_routes: rendering the error straight from the POST
    would leave the browser on a URL that re-submits the form on refresh.
    """
    return RedirectResponse(f"{path}?err={quote(msg)}", status_code=303)


# ============================== LIST + CREATE =============================


@router.get("/customers")
async def customers_page(
    request: Request,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    query = request.query_params.get("q", "")
    rows = await customer_view.build_rows(session, query)
    return render(
        request,
        "customers.html",
        {
            "user": user,
            "nav": "customers",
            "rows": rows,
            "q": query,
            "err": request.query_params.get("err"),
            # Codes minted by the redirect that landed here — shown once, so the
            # vendor can copy them before navigating away. They stay readable on
            # the detail page afterwards, so nothing is lost on refresh.
            "new_codes": request.query_params.getlist("code"),
        },
    )


@router.post("/customers")
async def create_customer_submit(
    num_nodes: int = Form(2),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Register a sold product: org + comfort config + N nodes + one code.

    No customer name or phone is entered here any more — a product is just a
    code plus the nodes it ships with. The buyer's identity arrives from the
    app when they redeem the code (auth_service.register fills the org's name
    and contact from what they type), so the vendor never re-keys it.

    The org is named after its code until then, so the still-unclaimed product
    is recognisable against the physical unit and the slip of paper the code
    was written on.

    The config row is seeded HERE because auth_service.register no longer does
    it — this handler is the only place an org is born. Without it the comfort
    worker has no knobs for the new household.

    One transaction: org + config + nodes + code commit together, so a failure
    never leaves a half-built product behind.
    """
    if not 0 <= num_nodes <= _MAX_NODES:
        return _err(f"Số node phải từ 0 đến {_MAX_NODES}")

    # Temporary name; replaced with the real code once it is generated below.
    org = await organization_service.create_org(
        session, name="…", org_type=OrgType.household
    )
    await config_service.create_default_config(session, org.id)

    # First node is the outdoor one: the comfort algorithm needs exactly one
    # outdoor reading (running-mean of outside temperature) and treats every
    # other node as an indoor room. The installer renames these on site.
    for i in range(num_nodes):
        is_outdoor = i == 0
        await device_service.create_device(
            session,
            org_id=org.id,
            name=f"Node {'ngoài trời' if is_outdoor else f'trong nhà {i}'}",
            node_type=NodeType.outdoor if is_outdoor else NodeType.indoor,
        )

    codes = await invite_service.create_codes(
        session, org_id=org.id, count=1, role=UserRole.owner
    )
    # Name the product after its code now that we have it — the label the
    # vendor sees until a customer registers.
    org.name = organization_service.product_label(codes[0].code)
    await session.commit()

    query = "&".join(f"code={quote(c.code)}" for c in codes)
    return RedirectResponse(f"/web/customers?{query}", status_code=303)


# ============================== DETAIL ====================================


@router.get("/customers/{org_id}")
async def customer_detail_page(
    org_id: uuid.UUID,
    request: Request,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    org = await organization_service.get_org(session, org_id)
    return render(
        request,
        "customer_detail.html",
        {
            "user": user,
            "nav": "customers",
            "org": org,
            "members": await user_service.list_by_org(session, org_id),
            "devices": await device_service.list_devices(session, org_id),
            "codes": await invite_service.list_for_org(session, org_id),
            "cfg": await config_service.get_config(session, org_id),
            "err": request.query_params.get("err"),
        },
    )


@router.get("/customers/{org_id}/devices/{device_id}")
async def customer_device_detail(
    org_id: uuid.UUID,
    device_id: uuid.UUID,
    request: Request,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """A customer's node, seen by vendor staff.

    Same page a household member gets for their own node — same readings,
    charts and history (web/device_view) — reached through the customer
    instead of through "my devices". The org_id comes from the URL and is
    handed to get_device, so asking for a device id that belongs to a
    different customer resolves to nothing rather than to their data.
    """
    org = await organization_service.get_org(session, org_id)
    try:
        device = await device_service.get_device(session, org_id, device_id)
    except AppException:
        return _err("Không tìm thấy node", f"/web/customers/{org_id}")

    return render(
        request,
        "device_detail.html",
        {
            "user": user,
            "nav": "customers",
            "back_url": f"/web/customers/{org_id}",
            "back_label": org.name,
            **await device_view.build_context(session, org_id, device),
        },
    )


# The customer edit form was removed: name, phone and email now come from the
# app at registration (auth_service.register), so a manual edit here would just
# be a second source that drifts. organization_service.update_org still exists
# if a correction path is needed later.


@router.post("/customers/{org_id}/delete")
async def delete_customer_submit(
    org_id: uuid.UUID,
    confirm_name: str = Form(""),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Delete a customer and everything under them.

    Postgres cascades this to their users, devices, telemetry, commands,
    comfort logs, IR codes, config and invite codes (every FK to
    organizations.id is ON DELETE CASCADE). There is no undo and no soft
    delete in this schema, so the customer's whole history goes.

    Hence typing the name: a stray click on a row's delete button is a
    plausible accident, and a confirm() dialog is one click away from the same
    accident. Naming the customer you are erasing is not.

    Refuses the staff member's own org: that is where vendor accounts live, and
    cascading it would delete the colleagues logged in right now.
    """
    org = await organization_service.get_org(session, org_id)

    if org_id == user.org_id:
        return _err(
            "Không thể xoá tổ chức chứa tài khoản quản trị của bạn",
            f"/web/customers/{org_id}",
        )
    if confirm_name.strip() != org.name:
        return _err(
            "Tên xác nhận không khớp — nhập đúng tên khách hàng để xoá",
            f"/web/customers/{org_id}",
        )

    await session.delete(org)
    await session.commit()
    # The list every other staff member is looking at just lost a row.
    await live_events.publish_vendor_change()
    return RedirectResponse("/web/customers", status_code=303)


# ============================== CONFIG ====================================


@router.post("/customers/{org_id}/config")
async def customer_config_submit(
    org_id: uuid.UUID,
    ema_alpha: float = Form(...),
    deadband: float = Form(...),
    dwell_sec: int = Form(...),
    dry_rh: float = Form(...),
    humid_slope: float = Form(...),
    clamp_min: float = Form(...),
    clamp_max: float = Form(...),
    override_hours: int = Form(...),
    night_start: int = Form(...),
    night_end: int = Form(...),
    night_offset: float = Form(...),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Tune one customer's comfort algorithm.

    Moved from /web/config, which tuned the STAFF member's own org. Validation
    stays in ConfigUpdate/config_service — the form only names the org.
    """
    await organization_service.get_org(session, org_id)  # 404 on a bogus org id
    data = ConfigUpdate(
        ema_alpha=ema_alpha,
        deadband=deadband,
        dwell_sec=dwell_sec,
        dry_rh=dry_rh,
        humid_slope=humid_slope,
        clamp_min=clamp_min,
        clamp_max=clamp_max,
        override_hours=override_hours,
        night_start=night_start,
        night_end=night_end,
        night_offset=night_offset,
    )
    try:
        await config_service.update_config(session, org_id, data)
    except AppException as exc:
        return _err(exc.detail, f"/web/customers/{org_id}")
    return RedirectResponse(f"/web/customers/{org_id}", status_code=303)


# ============================== CODES =====================================


@router.post("/customers/{org_id}/codes")
async def add_codes_submit(
    org_id: uuid.UUID,
    count: int = Form(1),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Issue extra codes — a household with a second phone needs a second code."""
    await organization_service.get_org(session, org_id)  # 404 on a bogus org id
    if not 1 <= count <= _MAX_CODES:
        return _err(f"Số mã phải từ 1 đến {_MAX_CODES}", f"/web/customers/{org_id}")
    await invite_service.create_codes(
        session, org_id=org_id, count=count, role=UserRole.owner
    )
    await session.commit()
    return RedirectResponse(f"/web/customers/{org_id}", status_code=303)


@router.post("/customers/{org_id}/codes/{code_id}/delete")
async def delete_code_submit(
    org_id: uuid.UUID,
    code_id: uuid.UUID,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Remove an over-generated unused code. Used codes are refused by the
    service (they are an activation record); see invite_service.delete_code."""
    try:
        await invite_service.delete_code(session, org_id, code_id)
    except AppException as exc:
        return _err(exc.detail, f"/web/customers/{org_id}")
    await session.commit()
    return RedirectResponse(f"/web/customers/{org_id}", status_code=303)


# ============================== NODES =====================================


@router.post("/customers/{org_id}/devices")
async def add_device_submit(
    org_id: uuid.UUID,
    name: str = Form(...),
    node_type: str = Form("indoor"),
    location: str = Form(""),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    await organization_service.get_org(session, org_id)
    if not name.strip():
        return _err("Tên node không được để trống", f"/web/customers/{org_id}")
    try:
        kind = NodeType(node_type)
    except ValueError:
        return _err("Loại node không hợp lệ", f"/web/customers/{org_id}")
    await device_service.create_device(
        session,
        org_id=org_id,
        name=name.strip(),
        node_type=kind,
        location=location.strip() or None,
    )
    await session.commit()
    return RedirectResponse(f"/web/customers/{org_id}", status_code=303)


@router.post("/customers/{org_id}/devices/{device_id}/edit")
async def edit_device_submit(
    org_id: uuid.UUID,
    device_id: uuid.UUID,
    name: str = Form(...),
    node_type: str = Form("indoor"),
    location: str = Form(""),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    try:
        device = await device_service.get_device(session, org_id, device_id)
        kind = NodeType(node_type)
    except (AppException, ValueError):
        return _err("Không tìm thấy node hoặc loại không hợp lệ", f"/web/customers/{org_id}")
    if not name.strip():
        return _err("Tên node không được để trống", f"/web/customers/{org_id}")
    await device_service.update_device(
        session, device, name=name.strip(), node_type=kind, location=location.strip()
    )
    await session.commit()
    return RedirectResponse(f"/web/customers/{org_id}", status_code=303)


@router.post("/customers/{org_id}/devices/{device_id}/role")
async def set_device_role_submit(
    org_id: uuid.UUID,
    device_id: uuid.UUID,
    role: str = Form(""),
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Set the node's network role (master/slave), or clear it (empty = chưa gán).
    Promoting to master demotes any other master in the household (device_service)."""
    try:
        device = await device_service.get_device(session, org_id, device_id)
        node_role = NodeRole(role) if role else None
    except (AppException, ValueError):
        return _err("Không tìm thấy node hoặc vai trò không hợp lệ", f"/web/customers/{org_id}")
    await device_service.set_role(session, device, node_role)
    await session.commit()
    return RedirectResponse(f"/web/customers/{org_id}/devices/{device_id}", status_code=303)


@router.post("/customers/{org_id}/devices/{device_id}/rotate")
async def rotate_device_creds_submit(
    org_id: uuid.UUID,
    device_id: uuid.UUID,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Issue new MQTT credentials for re-flashing a board. The old ones stop
    working on commit — the provisioning panel shows the new values to flash."""
    try:
        device = await device_service.get_device(session, org_id, device_id)
    except AppException:
        return _err("Không tìm thấy node", f"/web/customers/{org_id}")
    await device_service.rotate_credentials(session, device)
    await session.commit()
    return RedirectResponse(f"/web/customers/{org_id}/devices/{device_id}", status_code=303)


@router.post("/customers/{org_id}/devices/{device_id}/delete")
async def delete_device_submit(
    org_id: uuid.UUID,
    device_id: uuid.UUID,
    user: User = Depends(require_sysadmin),
    session: AsyncSession = Depends(get_db),
):
    """Remove a node the customer no longer has.

    Its telemetry/commands go with it (FK ON DELETE CASCADE). That is the point
    — a node that was physically removed should stop appearing in the
    household's history — but it is why the template asks for confirmation.
    """
    try:
        device = await device_service.get_device(session, org_id, device_id)
    except AppException:
        return _err("Không tìm thấy node", f"/web/customers/{org_id}")
    await device_service.delete_device(session, device)
    await session.commit()
    return RedirectResponse(f"/web/customers/{org_id}", status_code=303)
