"""LWT ``status`` handler — device online/offline presence (phase-04 step 7).

Payload is a plain literal string (``"online"``/``"offline"``), NOT JSON —
this is the broker-generated Last Will payload, so the consumer dispatch
loop hands it over undecoded (see ``mqtt_consumer._dispatch``).
"""

import logging

from app.core.database import AsyncSessionLocal
from app.core.tenant import set_current_org
from app.models.base import utcnow
from app.models.device import DeviceStatus
from app.services import live_events, telemetry_service
from app.utils.mqtt_naming import ParsedTopic

logger = logging.getLogger("breezelink.worker.status")

_VALID = {"online": DeviceStatus.online, "offline": DeviceStatus.offline}


async def handle_status(
    client, topic: ParsedTopic, payload: str, *, retained: bool = False
) -> None:
    """Cập nhật ``devices.status`` (+ ``last_seen_at``) từ một bản tin presence.

    ``retained=True`` nghĩa là broker PHÁT LẠI bản cuối cùng nó giữ, không phải
    node vừa nói. Phân biệt hai thứ đó là bắt buộc:

    Topic `status` publish có cờ retain, nên mỗi lần worker nối lại MQTT (deploy,
    khởi động lại, mạng chập chờn) broker dội về bản `"online"` cuối cùng của MỌI
    node — kể cả node đã rút điện từ hôm trước. Nếu ta đóng dấu ``last_seen_at =
    now()`` cho những bản tin đó thì đồng hồ tươi trong ``device_presence`` bị
    khởi động lại bởi một bản tin KHÔNG MANG THỜI GIAN, và cả bốn node phòng lại
    hiện "trực tuyến" thêm 150 giây nữa — lặp lại sau mỗi lần reconnect.

    Nên: bản tin retained được phép sửa CỜ ``status`` (đó là trạng thái broker
    biết), nhưng KHÔNG được làm mới ``last_seen_at`` (đó là câu "vừa nghe thấy",
    mà ta thì không hề nghe thấy gì).
    """
    status = _VALID.get(payload.strip().lower())
    if status is None:
        logger.warning("Unrecognized status payload %r on org=%s uuid=%s", payload, topic.org_id, topic.device_uuid)
        return

    async with AsyncSessionLocal() as session:
        set_current_org(topic.org_id)
        # Org-checked: presence must not be settable from another org's topic.
        device = await telemetry_service.get_device_for_topic(
            session, topic.org_id, topic.device_uuid
        )
        if device is None:
            logger.warning("No device registered for uuid=%s (org=%s)", topic.device_uuid, topic.org_id)
            return
        device.status = status
        if not retained:
            device.last_seen_at = utcnow()
        await session.commit()

    # Push the online/offline change to the realtime feed immediately.
    await live_events.publish_change(topic.org_id)

    # ...and to the vendor channel, or the ADMIN never sees it live.
    # The org nudge above only reaches sockets whose token carries THIS org —
    # i.e. the household's own app. Vendor staff watching this customer's page
    # are authenticated in a different org, so their socket subscribes to a
    # different channel: unplug a node and the admin page sat stale until
    # someone pressed refresh.
    #
    # NHỊP ĐÃ ĐỔI — chú thích cũ ghi "presence chỉ đổi lúc connect/disconnect"
    # và điều đó KHÔNG CÒN ĐÚNG. Nay cả gateway lẫn các slave đều gửi `status`
    # định kỳ 60 giây (STATUS_REFRESH_MS ở firmware), vì `last_seen_at` là thứ
    # device_presence dùng để quyết định trực tuyến. Nên đường này chạy khoảng
    # 1 lần/phút/node thay vì chỉ khi cắm/rút — vẫn rẻ (một nudge chuỗi rỗng),
    # nhưng đừng đọc nó như một sự kiện hiếm nữa.
    await live_events.publish_vendor_change("devices")
