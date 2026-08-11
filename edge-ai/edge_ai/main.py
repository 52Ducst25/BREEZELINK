"""Entry point: `python -m edge_ai.main`.

Two coroutines side by side: the BLE link (reconnects forever, feeds snapshots
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
from edge_ai.ble_client import GatewayLink
from edge_ai.controller import Controller


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
            # Chờ CÓ THỂ NGẮT thay vì sleep thẳng: SIGTERM được đáp ứng ngay
            # thay vì phải chờ hết nhịp 30s, và systemd sẽ giết cứng nếu chờ lâu
            # — giết cứng giữa lúc đang ghi một lệnh BLE là một lệnh gửi dở.
            await asyncio.wait_for(stop.wait(), timeout=tick_sec)
        except asyncio.TimeoutError:
            pass


async def _run(settings) -> int:
    log = logging.getLogger("edge")
    link = GatewayLink(settings)
    controller = Controller(settings, link)

    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGTERM, signal.SIGINT):
        try:
            loop.add_signal_handler(sig, stop.set)
        except NotImplementedError:
            # Windows không có add_signal_handler cho SIGTERM. Chạy thật là trên
            # Debian của UNO Q; trên máy dev thì Ctrl-C vẫn ném KeyboardInterrupt.
            pass

    log.info(
        "Edge AI chạy · org=%s · gateway=%s · nhịp %.0fs · giành lái sau %.0fs%s",
        settings.org_id, settings.gateway_address or "(tự quét)",
        settings.tick_sec, settings.takeover_after_sec,
        " · CHỈ ĐỀ XUẤT" if settings.advisory_only else "",
    )

    ble_task = asyncio.create_task(link.run(controller.on_snapshot))
    ctl_task = asyncio.create_task(_control_loop(controller, settings.tick_sec, stop))

    await stop.wait()
    ble_task.cancel()
    ctl_task.cancel()
    await asyncio.gather(ble_task, ctl_task, return_exceptions=True)
    log.info("Đã dừng")
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
