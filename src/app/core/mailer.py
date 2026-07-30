"""Password-reset email transport (Phase 3 — forgot-password parity).

SMTP is OPTIONAL in dev/test: if ``smtp_host`` isn't configured, the reset
link is logged instead of sent. ``POST /auth/forgot-password`` must never
fail (and must never leak whether an email exists) just because no mail
relay is wired up yet — see auth_routes.forgot_password.

Uses stdlib ``smtplib`` off the event loop via ``asyncio.to_thread`` — no
extra async-SMTP dependency needed for a single low-volume transactional
email.
"""

import asyncio
import logging
import smtplib
from email.message import EmailMessage

from app.config import get_settings

logger = logging.getLogger("breezelink.mailer")


def _build_message(to: str, token: str, link: str) -> EmailMessage:
    settings = get_settings()
    msg = EmailMessage()
    msg["Subject"] = "BreezeLink - Đặt lại mật khẩu"
    msg["From"] = settings.smtp_from
    msg["To"] = to
    msg.set_content(
        "Bạn (hoặc ai đó) vừa yêu cầu đặt lại mật khẩu tài khoản BreezeLink.\n\n"
        f"Mở liên kết sau trong app/trình duyệt để đặt mật khẩu mới "
        f"(hết hạn sau 30 phút):\n{link}\n\n"
        f"Token đặt lại: {token}\n\n"
        "Nếu bạn không yêu cầu việc này, hãy bỏ qua email — mật khẩu hiện tại "
        "vẫn giữ nguyên."
    )
    return msg


def _send_sync(msg: EmailMessage) -> None:
    """Blocking SMTP send — always run via ``asyncio.to_thread``."""
    settings = get_settings()
    with smtplib.SMTP(settings.smtp_host, settings.smtp_port, timeout=10) as smtp:
        smtp.starttls()
        if settings.smtp_user and settings.smtp_pass:
            smtp.login(settings.smtp_user, settings.smtp_pass)
        smtp.send_message(msg)


async def send_reset_email(to: str, token: str) -> None:
    """Send the password-reset link, or log it if SMTP isn't configured.

    Never raises: a mail-relay outage must not surface as a 500 on the
    forgot-password endpoint (that endpoint always returns 202 regardless).
    """
    settings = get_settings()
    link = f"{settings.reset_password_url_base}?token={token}"
    if not settings.smtp_host:
        logger.info("SMTP not configured (dev mode) — reset link for %s: %s", to, link)
        return
    try:
        msg = _build_message(to, token, link)
        await asyncio.to_thread(_send_sync, msg)
    except Exception:  # noqa: BLE001 - transport failure must never bubble up
        logger.exception("Failed to send password-reset email to %s", to)
