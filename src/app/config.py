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
    # Prod broker = EMQX tự host trong Docker (docker-compose.vps.yml), plaintext
    # 1883. Đây là địa chỉ INTERNAL: api/worker gọi nhau qua tên service Docker.
    mqtt_host: str = "mosquitto"
    mqtt_port: int = 8883
    mqtt_tls: bool = True

    # Địa chỉ broker mà THIẾT BỊ ngoài mạng dùng — KHÁC với mqtt_host ở trên.
    #
    # Vì sao phải tách: mqtt_host là "emqx", tên service Docker, chỉ phân giải
    # được bên trong compose network. Panel "Nạp firmware" lại in thẳng giá trị
    # đó ra cho người lắp chép vào config.h, nên node thật ôm một hostname không
    # bao giờ phân giải được và im lặng không kết nối — đã xảy ra thật khi lắp 2
    # node đầu tiên, phải sửa tay mới chạy.
    #
    # Để trống thì panel tự lùi về mqtt_host/mqtt_port (giữ nguyên hành vi cũ
    # cho môi trường dev, nơi hai địa chỉ trùng nhau).
    mqtt_public_host: str = ""
    mqtt_public_port: int = 0
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
    # Mặc định là VÍ DỤ, không phải địa chỉ thật: repo công khai. Giá trị dùng
    # thật đặt trong .env (SMTP_FROM / RESET_PASSWORD_URL_BASE) — xem .env.example.
    smtp_from: str = "no-reply@vi-du.com"
    reset_password_url_base: str = "https://quan-tri.vi-du.com/web/reset-password"


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
