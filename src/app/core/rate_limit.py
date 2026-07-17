"""Login/register brute-force throttle (M4 security gate, Phase 1).

A public-facing auth endpoint with no request throttle is an open invitation
to credential-stuffing. ``slowapi`` (a thin FastAPI wrapper over the
``limits`` library) tracks hit counts in-process, keyed by client IP —
sufficient for a single-instance deployment behind the Cloudflare Tunnel;
swap ``storage_uri`` to the shared Redis instance if the API is ever scaled
to multiple workers/replicas.

Wired into ``main.py`` (``app.state.limiter`` + 429 handler) and applied to
``POST /auth/login`` + ``POST /auth/register`` in ``api/v1/auth_routes.py``.
"""

from slowapi import Limiter
from slowapi.util import get_remote_address
from starlette.requests import Request


def _client_ip(request: Request) -> str:
    """Real caller IP behind the Cloudflare Tunnel.

    H2 fix: ``get_remote_address`` sees only the tunnel's loopback address
    (127.0.0.1), so every request would share ONE bucket — throttling the
    whole world together (attacker unthrottled, honest users locked out).
    Cloudflare sets ``CF-Connecting-IP`` to the true client; fall back to the
    first ``X-Forwarded-For`` hop, then the socket address for direct/dev runs.
    """
    cf_ip = request.headers.get("CF-Connecting-IP")
    if cf_ip:
        return cf_ip.strip()
    forwarded = request.headers.get("X-Forwarded-For")
    if forwarded:
        return forwarded.split(",")[0].strip()
    return get_remote_address(request)


limiter = Limiter(key_func=_client_ip)
