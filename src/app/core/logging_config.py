"""Structured logging config with secret masking.

The masking filter redacts values of sensitive keys (token / password) that leak
into log messages.
"""

import logging
import re

_SENSITIVE = re.compile(
    r"(?i)(token|password|secret|authorization)([\"'=:\s]+)([^\s,\"'}]+)"
)


class SecretMaskingFilter(logging.Filter):
    """Redact obvious secrets in the formatted log message."""

    def filter(self, record: logging.LogRecord) -> bool:
        try:
            message = record.getMessage()
        except Exception:  # pragma: no cover - defensive
            return True
        masked = _SENSITIVE.sub(r"\1\2***", message)
        if masked != message:
            record.msg = masked
            record.args = ()
        return True


def configure_logging(level: str = "INFO") -> None:
    """Configure root logging with the masking filter attached."""
    handler = logging.StreamHandler()
    handler.setFormatter(
        logging.Formatter("%(asctime)s %(levelname)s %(name)s %(message)s")
    )
    handler.addFilter(SecretMaskingFilter())

    root = logging.getLogger()
    root.handlers.clear()
    root.addHandler(handler)
    root.setLevel(level.upper())
