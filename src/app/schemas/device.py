"""Device DTOs: create/read representations for the devices API."""

import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict, model_validator

from app.models.device import DeviceStatus
from app.models.enums import NodeType
from app.services import device_presence


class DeviceCreate(BaseModel):
    """Payload to register a new device (outdoor or indoor node)."""

    name: str
    node_type: NodeType
    location: str | None = None


class DeviceRead(BaseModel):
    """Device representation returned by the API."""

    model_config = ConfigDict(from_attributes=True)

    id: uuid.UUID
    org_id: uuid.UUID
    name: str
    node_type: NodeType
    location: str | None
    status: DeviceStatus
    last_seen_at: datetime | None
    created_at: datetime

    @model_validator(mode="after")
    def _downgrade_stale_status(self) -> "DeviceRead":
        """Hạ ``status`` xuống offline khi ``last_seen_at`` đã cũ.

        PHẢI Ở ĐÂY, KHÔNG PHẢI Ở TỪNG ROUTE: cột ``devices.status`` là một lá cờ
        DÍNH — node phòng/ngoài trời không có phiên MQTT riêng, gateway publish
        hộ chúng, nên gateway chết là cờ của chúng đóng băng ở "online" mãi mãi.

        Đặt luật ngay trong DTO nghĩa là MỌI đường ra API đều đã đúng, kể cả
        route viết sau này. Bản vá trước đó chỉ sửa hai chỗ hiển thị của web và
        bỏ sót đúng đường này — app điện thoại vẫn hiện 3 node "Online" trong khi
        chỉ có mỗi gateway cắm điện.

        Xem services/device_presence.py cho ngưỡng và lý do chọn nó.
        """
        if self.status == DeviceStatus.online and not device_presence.is_fresh(
            self.last_seen_at
        ):
            self.status = DeviceStatus.offline
        return self
