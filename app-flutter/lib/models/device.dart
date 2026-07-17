import 'parse_utils.dart';

enum NodeType { outdoor, indoor, unknown }

NodeType nodeTypeFromWire(String? v) => switch (v) {
      'outdoor' => NodeType.outdoor,
      'indoor' => NodeType.indoor,
      _ => NodeType.unknown,
    };

extension NodeTypeLabel on NodeType {
  String get label => switch (this) {
        NodeType.outdoor => 'Ngoài trời',
        NodeType.indoor => 'Trong nhà',
        NodeType.unknown => 'Không rõ',
      };
}

enum DeviceOnlineStatus { online, offline, unknown }

DeviceOnlineStatus deviceStatusFromWire(String? v) => switch (v) {
      'online' => DeviceOnlineStatus.online,
      'offline' => DeviceOnlineStatus.offline,
      _ => DeviceOnlineStatus.unknown,
    };

/// Mirrors backend `DeviceRead` (`schemas/device.py`).
class Device {
  const Device({
    required this.id,
    required this.name,
    required this.nodeType,
    required this.location,
    required this.status,
    required this.lastSeenAt,
  });

  final String id;
  final String name;
  final NodeType nodeType;
  final String? location;
  final DeviceOnlineStatus status;
  final DateTime? lastSeenAt;

  factory Device.fromJson(Map<String, dynamic> json) {
    return Device(
      id: asString(json['id']),
      name: asString(json['name'], 'Thiết bị'),
      nodeType: nodeTypeFromWire(asStringOrNull(json['node_type'])),
      location: asStringOrNull(json['location']),
      status: deviceStatusFromWire(asStringOrNull(json['status'])),
      lastSeenAt: asDateTimeOrNull(json['last_seen_at']),
    );
  }
}
