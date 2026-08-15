"""Decides whether the cloud is still driving, and therefore whether this node
should keep quiet or take the wheel.

THE WHOLE POINT IS THAT EXACTLY ONE SIDE COMMANDS AT A TIME. If the cloud worker
and this node both drive the air conditioner, it gets two contradicting orders a
minute apart, the setpoint appears to change on its own, and the symptom looks
exactly like a comfort-algorithm bug — sending whoever debugs it into the wrong
half of the system.

So the rule is asymmetric on purpose:

  take over   only after a LONG silence (default 300 s ≈ 20 telemetry ticks)
  hand back   as soon as the gateway reports hearing the cloud again

Being slow to take over costs a few minutes of non-adaptive comfort. Being slow
to hand back costs a fight over the compressor. Those are not the same price.

THE SILENCE IS MEASURED BY THE GATEWAY, NOT HERE. The gateway holds the MQTT
session, so it knows exactly how long since the server last commanded; this node
only sees what the gateway reports (``cloud_silence_sec``). That also removes a
whole class of bug the MQTT-based design had: this node used to subscribe to the
very topic it published on, so its own takeover command echoed back and read as
"the cloud is alive" — handing control back one tick after taking it, forever.
There is no echo to confuse now, because the gateway counts only SERVER commands.
"""

import logging
import time

logger = logging.getLogger("edge.cloud")


class CloudWatch:
    def __init__(self, takeover_after_sec: float) -> None:
        self._takeover_after = takeover_after_sec
        self._driving = False
        self._silence: float | None = 0.0
        # When this service started observing. See the "never commanded" branch in
        # update() for why it is needed.
        self._watching_since = time.monotonic()

    def update(self, silence_sec: int | None) -> None:
        """Feed in `cloud_silence_sec` from the latest snapshot.

        None means the server has NEVER commanded this household -- quite different
        from "it just commanded". Treat it as infinite silence: in a freshly installed
        household the cloud has never touched, there is nobody to defer to, and that
        is exactly the case where the fallback layer should be working.
        """
        previously = self._driving
        self._silence = None if silence_sec is None else float(silence_sec)

        if self._silence is not None and self._silence < self._takeover_after:
            self._driving = False
            if previously:
                logger.info("RELEASING CONTROL - the server has spoken again (%.0fs ago)", self._silence)
            return

        # "NEVER COMMANDED" MUST NOT TAKE CONTROL THE INSTANT WE BOOT.
        #
        # An earlier version treated None as infinite silence and took over
        # immediately -- measured on real hardware it took control EXACTLY 4 SECONDS
        # after the service started, while the gateway's MQTT session was perfectly
        # healthy. That is precisely the "both sides driving" case this entire file
        # was written to avoid.
        #
        # Why the earlier version was wrong: None says "the server has not commanded
        # SINCE THE GATEWAY BOOTED" -- it cannot distinguish "the cloud is dead" from
        # "the cloud has had nothing to command". Right after the whole house is
        # powered on those two look identical, and only time separates them.
        #
        # So: we have to OBSERVE FOR OURSELVES for that same threshold before drawing
        # a conclusion. Exactly the asymmetry principle at the top of this file --
        # taking over late costs a few minutes without adaptation, taking over wrongly
        # means both sides fight over the compressor.
        if self._silence is None:
            watched = time.monotonic() - self._watching_since
            if watched < self._takeover_after:
                self._driving = False
                return

        self._driving = True
        if not previously:
            logger.warning(
                "TAKING CONTROL - the server has been silent %s (threshold %.0fs)",
                "and has never commanded; we have now observed for the full threshold"
                if self._silence is None
                else f"for {self._silence:.0f}s",
                self._takeover_after,
            )

    @property
    def driving(self) -> bool:
        return self._driving

    @property
    def silence_sec(self) -> float | None:
        return self._silence
