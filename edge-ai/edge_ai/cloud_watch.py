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

logger = logging.getLogger("edge.cloud")


class CloudWatch:
    def __init__(self, takeover_after_sec: float) -> None:
        self._takeover_after = takeover_after_sec
        self._driving = False
        self._silence: float | None = 0.0

    def update(self, silence_sec: int | None) -> None:
        """Nạp `cloud_silence_sec` từ ảnh chụp mới nhất.

        None nghĩa là máy chủ CHƯA TỪNG ra lệnh cho hộ này — khác hẳn "vừa ra
        lệnh xong". Coi nó là im lặng vô hạn: một hộ vừa lắp mà cloud chưa bao
        giờ chạm tới thì không có ai để mà nhường, và đó đúng là ca lớp dự phòng
        nên làm việc.
        """
        previously = self._driving
        self._silence = None if silence_sec is None else float(silence_sec)

        if self._silence is not None and self._silence < self._takeover_after:
            self._driving = False
            if previously:
                logger.info("NHẢ LÁI — máy chủ đã lên tiếng trở lại (%.0fs trước)", self._silence)
            return

        self._driving = True
        if not previously:
            logger.warning(
                "GIÀNH LÁI — máy chủ im lặng %s (ngưỡng %.0fs)",
                "chưa từng ra lệnh" if self._silence is None else f"{self._silence:.0f}s",
                self._takeover_after,
            )

    @property
    def driving(self) -> bool:
        return self._driving

    @property
    def silence_sec(self) -> float | None:
        return self._silence
