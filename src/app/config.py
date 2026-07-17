"""Application configuration.

Pydantic Settings singleton read from ``.env`` and cached in RAM.

TRAP #3: Settings are cached via ``lru_cache``. Changing ``.env`` requires a
restart of BOTH the api and the worker processes for changes to take effect.
"""

from functools import lru_cache

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """Runtime configuration loaded from environment / ``.env``."""

    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
        case_sensitive=False,
    )

    # --- App ---
    app_name: str = "AirConditioner"
    environment: str = "development"
    log_level: str = "INFO"

    # --- Database (async asyncpg driver) ---
    db_url: str = "postgresql+asyncpg://aircon:aircon@postgres:5432/aircon"

    # --- Redis (state/setpoint/ircache/override cache — activated Phase 2) ---
    redis_url: str = "redis://redis:6379/0"

    # --- JWT (HS256) ---
    jwt_secret: str = "change-me-in-production"
    jwt_alg: str = "HS256"
    access_token_ttl_minutes: int = 30
    refresh_token_ttl_days: int = 14
    # SSR admin cookie lifetime. Separate from access_token_ttl_minutes: the
    # browser has no refresh-token rotation, so this is a real working session,
    # not a token that something else keeps alive (see
    # core/security.create_web_session_token).
    web_session_ttl_hours: int = 12

    # --- MQTT / MQTTS ---
    # Prod broker = EMQX Serverless (cloud managed, e.g. <cluster>.ap1.emqxsl.com),
    # MQTTS on port 8883 with TLS required (infra decision: no self-hosted EMQX).
    # Dev: docker-compose overrides these to a local eclipse-mosquitto broker on
    # plaintext 1883 (see docker/docker-compose.yml) — TLS is not needed locally.
    mqtt_host: str = "mosquitto"
    mqtt_port: int = 8883
    mqtt_tls: bool = True
    mqtt_user: str = "aircon_worker"
    mqtt_pass: str = "worker-secret"
    mqtt_keepalive: int = 30

    # --- Worker ---
    worker_heartbeat_seconds: int = 60

    # --- Mailer (SMTP) — password-reset email transport (Phase 3) ---
    # Dev: leave smtp_host unset -> mailer logs the reset link instead of
    # sending (graceful degrade, see core/mailer.py). Prod: point at a real
    # relay (Gmail app-password, Mailgun, SES SMTP, ...).
    smtp_host: str | None = None
    smtp_port: int = 587
    smtp_user: str | None = None
    smtp_pass: str | None = None
    smtp_from: str = "no-reply@vi-du.com"
    reset_password_url_base: str = "https://admin.vi-du.com/reset-password"


_DEV_TEST_ENVIRONMENTS = {"development", "test", "testing"}
_DEFAULT_SECRETS = {
    "jwt_secret": "change-me-in-production",
    "mqtt_pass": "worker-secret",
}


@lru_cache
def get_settings() -> Settings:
    """Return the cached ``Settings`` singleton.

    M2 fix: fail fast at startup if either secret is still the shipped
    placeholder outside dev/test — a forgeable default JWT signing key or
    MQTT worker password reaching production is a real compromise, not a
    theoretical one. Dev/test keep working with the convenience defaults.
    """
    settings = Settings()
    if settings.environment.lower() not in _DEV_TEST_ENVIRONMENTS:
        for field, default in _DEFAULT_SECRETS.items():
            if getattr(settings, field) == default:
                raise RuntimeError(
                    f"{field} is still the default placeholder value — set a real "
                    f"secret before running outside development/test "
                    f"(environment={settings.environment!r})"
                )
    return settings
