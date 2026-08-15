"""Entry point: `python -m edge_ai.main`.

Two coroutines side by side: the UART link (reconnects forever, feeds snapshots
into the controller) and the control loop (fixed cadence, decides and acts).

They are SEPARATE ON PURPOSE. Deciding inside the notify callback would tie the
decision rate to the gateway's push rate — and worse, would run comfort maths on
the Bluetooth stack's own task, where a slow tick delays the next notification.
"""

import asyncio
import logging
import os
import signal
import sys

from edge_ai import config
from edge_ai.controller import Controller


def _make_link(settings):
    """Build the link to the gateway according to the configuration.

    Imported LATE rather than importing both at the top of the file: the `serial`
    version needs pyserial, the `bridge` version needs App Lab's `arduino` package,
    and no environment has both. Importing at the top would make whichever one is
    running die for want of the other one's library.
    """
    if settings.link == "bridge":
        from edge_ai.bridge_client import GatewayLink
    else:
        from edge_ai.uart_client import GatewayLink
    return GatewayLink(settings)


def _load_dotenv() -> None:
    """Load a sibling ``.env`` if present, without adding a dependency.

    Hand-rolled rather than python-dotenv because this service should install on
    a field device with as few packages as possible. Values already in the
    environment WIN — systemd's own ``Environment=`` lines must not be silently
    overridden by a stale file left on disk.
    """
    path = os.path.join(os.path.dirname(os.path.dirname(__file__)), ".env")
    if not os.path.isfile(path):
        return
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


async def _control_loop(controller: Controller, tick_sec: float, stop: asyncio.Event) -> None:
    while not stop.is_set():
        await controller.tick()
        try:
            # An INTERRUPTIBLE wait rather than a plain sleep: SIGTERM is honoured
            # immediately instead of waiting out the 30s tick, and systemd hard-kills
            # anything that takes too long -- a hard kill in the middle of writing a
            # UART command leaves a half-sent command.
            await asyncio.wait_for(stop.wait(), timeout=tick_sec)
        except asyncio.TimeoutError:
            pass


async def _run(settings) -> int:
    log = logging.getLogger("edge")
    link = _make_link(settings)
    controller = Controller(settings, link)

    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGTERM, signal.SIGINT):
        try:
            loop.add_signal_handler(sig, stop.set)
        except NotImplementedError:
            # Windows has no add_signal_handler for SIGTERM. Production runs on the
            # UNO Q's Debian; on a dev machine Ctrl-C still raises KeyboardInterrupt.
            pass

    log.info(
        "Edge AI running · org=%s · link=%s · tick %.0fs · take over after %.0fs%s",
        settings.org_id,
        "bridge (via the STM32 sketch)" if settings.link == "bridge"
        else f"serial {settings.uart_port or '(auto-detect)'}",
        settings.tick_sec, settings.takeover_after_sec,
        " · ADVISORY ONLY" if settings.advisory_only else "",
    )

    link_task = asyncio.create_task(link.run(controller.on_snapshot))
    ctl_task = asyncio.create_task(_control_loop(controller, settings.tick_sec, stop))

    await stop.wait()
    link_task.cancel()
    ctl_task.cancel()
    await asyncio.gather(link_task, ctl_task, return_exceptions=True)
    log.info("Stopped")
    return 0


def main() -> int:
    logging.basicConfig(
        level=os.getenv("EDGE_LOG_LEVEL", "INFO").upper(),
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
    )
    _load_dotenv()
    try:
        settings = config.load()
    except config.ConfigError as exc:
        # Exit 1, do not retry: a missing setting is not a transient fault, and
        # a service that restart-loops on it buries the one line explaining why.
        logging.getLogger("edge").error("%s", exc)
        return 1

    try:
        return asyncio.run(_run(settings))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
