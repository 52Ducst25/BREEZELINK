"""ORM model registry.

Importing this package ensures every model is registered on ``Base.metadata``
(required by Alembic migrations and test schema creation).
"""

from app.models.app_release import AppRelease
from app.models.base import Base
from app.models.comfort_log import ComfortLog
from app.models.command import Command
from app.models.config import Config
from app.models.device import Device, DeviceStatus
from app.models.enums import AcMode, CommandSource, NodeType
from app.models.invite_code import InviteCode
from app.models.ir_code import IrCode
from app.models.organization import Organization, OrgType
from app.models.telemetry import Telemetry
from app.models.user import User, UserRole

__all__ = [
    "Base",
    "AppRelease",
    "Organization",
    "OrgType",
    "User",
    "UserRole",
    "Device",
    "DeviceStatus",
    "NodeType",
    "CommandSource",
    "AcMode",
    "Telemetry",
    "InviteCode",
    "IrCode",
    "Command",
    "ComfortLog",
    "Config",
]
