# AirConditioner backend image — shared by api and worker (same image, different CMD).
FROM python:3.12-slim

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PIP_NO_CACHE_DIR=1

WORKDIR /code

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential curl \
    && rm -rf /var/lib/apt/lists/*

# Dependency manifest first so a source-only edit doesn't reinstall the world.
COPY pyproject.toml ./
COPY src ./src
RUN pip install --upgrade pip && pip install -e ".[dev]"

COPY alembic.ini ./

# Non-root runtime. SafeKitchen's image runs as root; there is no reason to.
RUN useradd --create-home --uid 1000 appuser

# Uploaded APKs live here, on a named volume (docker-compose*.yml: apk_data).
# The dir MUST exist and be owned by appuser IN THE IMAGE, before USER switches:
# Docker seeds an empty named volume from the image path — including its
# ownership — on first mount. Skip this and the volume is created root-owned,
# and the non-root api gets EACCES the first time someone publishes a release.
# (Only helps on FIRST creation; an already-root-owned volume needs removing.)
RUN mkdir -p /data/apk && chown -R appuser:appuser /data

USER appuser

EXPOSE 8000

# Compose overrides this for the worker and to prepend `alembic upgrade head`.
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]

# Lets compose gate cloudflared on a genuinely-serving api rather than a started one.
HEALTHCHECK --interval=15s --timeout=5s --start-period=25s --retries=4 \
    CMD curl -fsS http://127.0.0.1:8000/api/v1/health || exit 1
