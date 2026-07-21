import 'parse_utils.dart';

enum UserRole { owner, member, unknown }

UserRole userRoleFromWire(String? v) => switch (v) {
      'owner' => UserRole.owner,
      'member' => UserRole.member,
      _ => UserRole.unknown,
    };

/// Mirrors backend `MeResponse` (`schemas/auth.py`) — `GET /auth/me`.
class UserProfile {
  const UserProfile({
    required this.id,
    required this.email,
    required this.orgId,
    required this.role,
    this.fullName,
    this.phone,
    this.location,
    this.latitude,
    this.longitude,
    this.isSysadmin = false,
  });

  final String id;
  final String email;
  final String orgId;
  final UserRole role;
  final String? fullName;
  final String? phone;

  /// Household address, shown on the dashboard location card. Null/empty until
  /// the user sets it in Account.
  final String? location;

  /// Home coordinates picked on the map, null until set.
  final double? latitude;
  final double? longitude;

  /// Vendor staff, not a household member. Their org owns no nodes, so the app
  /// legitimately looks empty for them — the UI says so instead of leaving them
  /// to guess it is broken.
  final bool isSysadmin;

  bool get isOwner => role == UserRole.owner;

  factory UserProfile.fromJson(Map<String, dynamic> json) {
    return UserProfile(
      id: asString(json['id']),
      email: asString(json['email']),
      orgId: asString(json['org_id']),
      role: userRoleFromWire(asStringOrNull(json['role'])),
      fullName: asStringOrNull(json['full_name']),
      phone: asStringOrNull(json['phone']),
      location: asStringOrNull(json['location']),
      latitude: asDoubleOrNull(json['latitude']),
      longitude: asDoubleOrNull(json['longitude']),
      // Vắng field (server cũ) -> false: mặc định coi là tài khoản hộ gia đình,
      // tức là KHÔNG hiện banner nhân viên. Đoán nhầm theo hướng này thì chỉ
      // thiếu một lời nhắc; đoán nhầm hướng kia là doạ khách hàng thật.
      isSysadmin: asBool(json['is_sysadmin']),
    );
  }
}
