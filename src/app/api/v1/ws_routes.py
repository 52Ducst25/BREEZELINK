"""WebSocket live feed — replaces the app's 5s polling.

`GET /api/v1/ws/live?token=<access_jwt>`: authenticates from the query-string JWT
(WebSocket clients can't send an Authorization header pre-handshake), pushes an
initial state snapshot, then re-pushes a fresh snapshot whenever the worker signals
a state change on the org's Redis channel. A ~25s heartbeat keeps the connection
(and the Cloudflare tunnel) alive and doubles as disconnect detection.
"""

import logging
import uuid

from fastapi import APIRouter, Query, WebSocket, WebSocketDisconnect

from app.core.database import AsyncSessionLocal
from app.core.redis_client import get_redis
from app.core.security import (
    ACCESS_TOKEN_TYPE,
    SESSION_COOKIE_NAME,
    WEB_SESSION_TOKEN_TYPE,
    decode_token,
)
from app.services import live_events, live_state_service, user_service

router = APIRouter(tags=["ws"])
logger = logging.getLogger("breezelink.ws")

_HEARTBEAT_SEC = 25


async def _snapshot(org_id: str) -> dict:
    async with AsyncSessionLocal() as session:
        return await live_state_service.build_live_state(session, org_id)


@router.websocket("/ws/live")
async def ws_live(websocket: WebSocket, token: str | None = Query(None)) -> None:
    # Two transports, one endpoint: the Flutter app passes ?token= (a browser can't
    # set headers on the WS handshake), while the SSR admin has only an httpOnly
    # cookie — unreadable from JS, but the browser does send it on a same-origin
    # handshake. Query first so an explicit token always wins.
    # The two transports also carry two different token TYPES, and each is only
    # valid on its own path: ?token= is an API access token, the cookie is a
    # web-session token the API deliberately does not accept (see
    # core/security.WEB_SESSION_TOKEN_TYPE). Checking each against its own type
    # keeps that separation — validating both as "access" would hand the cookie
    # back its API powers through this endpoint.
    raw = token or websocket.cookies.get(SESSION_COOKIE_NAME)
    expected = ACCESS_TOKEN_TYPE if token else WEB_SESSION_TOKEN_TYPE
    if not raw:
        await websocket.close(code=1008)
        return

    # Auth BEFORE accept — reject bad tokens with a policy-violation close.
    try:
        payload = decode_token(raw, expected_type=expected)
        org_id = str(payload["org_id"])
    except Exception:
        await websocket.close(code=1008)
        return

    # Vendor staff on the SSR admin also watch the back-office channel, so a
    # customer redeeming a code in a DIFFERENT org refreshes their customer
    # list (see live_events.VENDOR_CHANNEL).
    #
    # Cookie transport only. The flag is not in the token — it is read from the
    # DB, once per connection — and the app's ?token= contract stays exactly as
    # it was: org snapshots, nothing else.
    watch_vendor = False
    if token is None:
        try:
            async with AsyncSessionLocal() as session:
                user = await user_service.get_by_id(session, uuid.UUID(payload["sub"]))
                watch_vendor = bool(user and user.is_sysadmin)
        except Exception:
            # A lookup failure must not cost the socket its normal feed; the
            # customer list just falls back to refresh-on-navigate.
            watch_vendor = False

    await websocket.accept()
    pubsub = get_redis().pubsub()
    await pubsub.subscribe(live_events.channel(org_id))
    if watch_vendor:
        await pubsub.subscribe(live_events.VENDOR_CHANNEL)
    # redis-py's first get_message() after subscribe returns None immediately
    # (a no-op read); consume it here so the loop's first real read blocks
    # properly instead of emitting a spurious ping right after connect.
    await pubsub.get_message(ignore_subscribe_messages=True, timeout=0.1)
    try:
        await websocket.send_json(await _snapshot(org_id))
        while True:
            # Block up to the heartbeat interval for a change nudge; on timeout
            # send a ping (also surfaces a dropped client via a send error).
            msg = await pubsub.get_message(ignore_subscribe_messages=True, timeout=_HEARTBEAT_SEC)
            if msg is None:
                await websocket.send_json({"type": "ping"})
                continue
            # A vendor nudge says "the customer list moved", which has nothing
            # to do with this org's live state — building an org snapshot for
            # it would be wasted work. Either way app.js only reads "not a
            # ping" and re-fetches the page it is on.
            if msg.get("channel") in (live_events.VENDOR_CHANNEL, live_events.VENDOR_CHANNEL.encode()):
                await websocket.send_json({"type": "customers"})
            else:
                await websocket.send_json(await _snapshot(org_id))
    except WebSocketDisconnect:
        pass
    except Exception as exc:  # client gone / send failure — end the session
        logger.debug("ws_live closed org=%s: %s", org_id, exc)
    finally:
        try:
            # No argument = unsubscribe from EVERY channel this pubsub holds.
            # There are now two (the org channel plus, for vendor staff, the
            # back-office channel), and the old code named a `ch` variable that
            # the vendor-channel refactor removed — a NameError that crashed
            # the WS teardown on every disconnect.
            await pubsub.unsubscribe()
        finally:
            try:
                await pubsub.aclose()
            except AttributeError:
                await pubsub.close()
