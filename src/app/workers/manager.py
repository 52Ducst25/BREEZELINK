"""Worker entrypoint (runs OUTSIDE the API process).

Usage:
    python -m app.workers.manager --role consumer   # MQTT ingest (stub in Phase 0)

Graceful shutdown: SIGTERM/SIGINT cancel the main task (Docker/Linux). On
Windows, add_signal_handler is unsupported, so we fall back to KeyboardInterrupt.
A heartbeat is logged every WORKER_HEARTBEAT_SECONDS for external watchdogs.
"""

import argparse
import asyncio
import contextlib
import logging
import signal

from app.config import get_settings
from app.core.database import engine
from app.core.logging_config import configure_logging
from app.workers.mqtt_consumer import run_consumer

logger = logging.getLogger("breezelink.worker")


async def _heartbeat(interval: int) -> None:
    while True:
        await asyncio.sleep(interval)
        logger.info("worker heartbeat")


async def _run(role: str) -> None:
    settings = get_settings()
    main_task = asyncio.create_task(run_consumer(), name=f"worker-{role}")
    hb_task = asyncio.create_task(_heartbeat(settings.worker_heartbeat_seconds))

    loop = asyncio.get_running_loop()

    def _shutdown() -> None:
        logger.info("Shutdown signal received — cancelling worker")
        main_task.cancel()

    for sig in (signal.SIGTERM, signal.SIGINT):
        try:
            loop.add_signal_handler(sig, _shutdown)
        except (NotImplementedError, RuntimeError):
            # Windows dev: signal handlers unsupported on the event loop.
            pass

    try:
        await main_task
    except asyncio.CancelledError:
        logger.info("Worker stopped gracefully")
    finally:
        hb_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await hb_task
        await engine.dispose()


def main() -> None:
    parser = argparse.ArgumentParser(description="BreezeLink worker manager")
    parser.add_argument(
        "--role",
        choices=["consumer"],
        default="consumer",
        help="consumer = MQTT ingest (real subscribe added in Phase 4)",
    )
    args = parser.parse_args()

    configure_logging(get_settings().log_level)
    logger.info("Starting worker role=%s", args.role)
    try:
        asyncio.run(_run(args.role))
    except KeyboardInterrupt:
        logger.info("Worker interrupted — exiting")


if __name__ == "__main__":
    main()
