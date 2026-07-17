import 'parse_utils.dart';

enum UserRole { owner, member, unknown }

UserRole userRoleFromWire(String? v) => switch (v) {
      'owner' => UserRole.owner,
      'member' => UserRole.member,
      _ => UserRole.unknown,
    };

/// Mirrors backend `MeResponse` (`schemas/auth.py`) — `GET /auth/me`.
class UserProfile {
  const UserProfile({required this.id, required this.email, required this.orgId, required this.role});

  final String id;
  final String email;
  final String orgId;
  final UserRole role;

  bool get isOwner => role == UserRole.owner;

  factory UserProfile.fromJson(Map<String, dynamic> json) {
    return UserProfile(
      id: asString(json['id']),
      email: asString(json['email']),
      orgId: asString(json['org_id']),
      role: userRoleFromWire(asStringOrNull(json['role'])),
    );
  }
}
