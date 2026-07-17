"""Device DTOs: create/read representations for the devices API."""

import uuid
from datetime import datetime

from pydantic import BaseModel, ConfigDict

from app.models.device import DeviceStatus
from app.models.enums import NodeType


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
