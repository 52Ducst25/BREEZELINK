"""The monitoring page on port 7000 -- seeing what the model is thinking.

WHY IT IS NEEDED: everything this node computes currently lives only in the log. To
find out what τ is, whether the forecast is accurate, or which corner is faulty, you
have to open the Python tab and read text. For a wall-mounted device that is not how
you answer the question "how is the house doing".

REST WITH A FEW-SECOND POLL, NOT WEBSOCKET, even though the brick offers both. Its
WebSocket channel runs on Socket.IO, and Socket.IO needs its own JavaScript library in
the browser -- that library is normally loaded from a CDN, and this is a device that
has to work with no network. The data only changes every 30 seconds so a 3-second poll
is more than enough; the trade buys a page that depends on nothing external.

DISABLEABLE AND SELF-DISABLING: the ``web_ui`` brick only exists inside App Lab's
container. Under systemd the import fails, and the service still has to run normally
then -- the monitoring page is a nice-to-have, not a precondition for controlling an
air conditioner.
"""

import logging
import threading
import time
from typing import Any

logger = logging.getLogger("edge.web")

# The maximum number of points returned for the chart. 240 points across a 1000px
# frame is over 4px per point -- drawing denser only makes the browser work harder
# without the eye seeing any difference.
MAX_CHART_POINTS = 240


class Dashboard:
    """A small web server for the monitoring page. The service runs fine without it."""

    def __init__(self, history) -> None:
        self._history = history
        self._web: Any = None
        self._state: dict[str, Any] = {"ready": False}

    @property
    def enabled(self) -> bool:
        return self._web is not None

    def start(self) -> None:
        """Bring up the server. Swallows every error -- see the note at the top of the file."""
        try:
            from arduino.app_bricks.web_ui import WebUI
        except ImportError:
            logger.info("No web_ui brick (not running inside App Lab) - skipping the monitoring page")
            return

        try:
            web = WebUI()
            web.expose_api("GET", "/api/state", lambda: self._state)
            web.expose_api("GET", "/api/history", self._history_json)
            web.start()
        except Exception as exc:  # noqa: BLE001
            # RuntimeError if assets/index.html is missing, OSError if the port is busy.
            logger.warning("Could not bring up the monitoring page: %s", exc)
            return

        # `start()` ONLY PREPARES -- the serving loop lives in `execute()`.
        #
        # A normal Arduino application calls `App.run()`, and AppController finds every
        # brick's `execute`/`loop` methods and runs each one in a thread. This service
        # has its own asyncio lifecycle so it does not use `App.run()`, and the
        # consequence is that nobody runs `execute()` on its behalf.
        #
        # The symptom when forgotten: `start()` returns cleanly, no error, no warning --
        # there is simply nothing listening on port 7000. This has bitten us exactly
        # once.
        #
        # A daemon thread: it has to die with the process. Otherwise App Lab's Stop
        # button sends SIGTERM, the asyncio side exits cleanly, and uvicorn keeps the
        # process alive forever until App Lab hard-kills it.
        #
        # ASSIGN self._web BEFORE starting the thread: the thread reads that attribute,
        # and starting first is a race we win most of the time -- the worst kind of bug.
        self._web = web
        threading.Thread(target=self._serve, name="web-ui", daemon=True).start()
        logger.info("Monitoring page: %s", getattr(web, "url", "http://<board-ip>:7000"))

    def _serve(self) -> None:
        try:
            self._web.execute()
        except Exception as exc:  # noqa: BLE001
            logger.warning("The monitoring page stopped: %s", exc)

    def publish(self, state: dict[str, Any]) -> None:
        """Replace the state snapshot the page will read on its next poll.

        ASSIGN A WHOLE NEW DICT rather than mutating in place: the HTTP handler runs on
        another thread, and mutating in place could let it catch a half-updated state.
        Assigning a reference is atomic.
        """
        self._state = state

    def _history_json(self) -> dict[str, Any]:
        """The last 6 hours for the chart, already decimated."""
        rows = self._history.recent(hours=6.0)
        if not rows:
            return {"points": []}
        step = max(1, len(rows) // MAX_CHART_POINTS)
        return {
            "points": [
                {"ts": r.ts, "t_in": round(r.t_in, 2),
                 "t_out": None if r.t_out is None else round(r.t_out, 2),
                 "sp": r.setpoint, "cool": r.cooling_demand > 0}
                for r in rows[::step]
            ]
        }


def build_state(*, snapshot, store, model, score, weather, cloud, settings,
                result, forecast, anomalies) -> dict[str, Any]:
    """Gather everything the page needs into a plain JSON dict.

    A PURE FUNCTION, deliberately separate from Controller: the presentation layer
    changes far more often than the control layer, and mixing them would mean every
    text-colour tweak requires rereading the code that commands the compressor.
    """
    now = time.time()
    peak = weather.peak_within(12.0) if weather is not None else None

    return {
        "ready": True,
        "ts": now,
        "indoor": {"t": snapshot.t_in, "h": snapshot.h_in},
        "outdoor": {"t": snapshot.t_out, "h": snapshot.h_out,
                    "online": snapshot.outdoor_online},
        "rooms": [
            {"label": store.label(slot), "t": temp}
            for slot, temp in sorted(store.latest_per_room().items())
        ],
        "ac": {"mode": snapshot.ac_mode.name_str, "setpoint": snapshot.ac_setpoint},
        "link": {"wifi": snapshot.wifi_up, "mqtt": snapshot.mqtt_up,
                 "override": snapshot.override_active},
        "control": {
            "driving": cloud.driving,
            "silence_sec": cloud.silence_sec,
            "takeover_after_sec": settings.takeover_after_sec,
            "advisory_only": settings.advisory_only,
            "want_mode": None if result is None else result.mode.value,
            "want_setpoint": None if result is None else result.t_set,
        },
        "model": {
            "ready": model.ready,
            "samples": model.samples,
            "tau_min": model.tau_min,
            "outdoor_gain": model.outdoor_gain,
            "cool_gain": model.cool_gain,
            "describe": model.describe(),
        },
        "score": {
            "n": score.n, "mae": score.mae, "naive_mae": score.naive_mae,
            "trustworthy": score.trustworthy, "describe": score.describe(),
        },
        "forecast_15": forecast,
        "weather": {
            "fresh": weather.fresh if weather is not None else False,
            "age_min": weather.age_min if weather is not None else None,
            "peak_t": None if peak is None else peak[0],
            "peak_ts": None if peak is None else peak[1],
            # The next 12 hours of forecast, one point per hour -- enough to see the shape.
            "hours": [] if weather is None else [
                {"ts": now + h * 3600, "t": weather.temp_at(now + h * 3600)}
                for h in range(13)
            ],
        },
        "anomalies": [f"{a.kind}: {a.detail}" for a in anomalies],
    }
