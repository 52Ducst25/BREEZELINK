import 'parse_utils.dart';

/// Mirrors backend `ConfigRead` (`schemas/config.py`) — owner-only algorithm
/// tunables. The app only reads [clampMin]/[clampMax] (the real, server-side
/// setpoint bounds) so the manual-override slider never offers a value the
/// server would reject. See `services/config_api.dart` for the role gate:
/// members cannot fetch this at all, and the app must NOT invent a fallback
/// range when that happens (brief rule — no hardcoded [16,30] guess).
class ConfigBounds {
  const ConfigBounds({required this.clampMin, required this.clampMax, required this.overrideHours});

  final double clampMin;
  final double clampMax;

  /// TTL the server applies to a manual override (`redis_override_service`).
  /// Also owner-only — used to show an ESTIMATED countdown; there is no
  /// GET-current-override-remaining-TTL endpoint, so this is always a
  /// client-side estimate starting from the moment the override was set,
  /// never a value read back from the server after the fact.
  final int overrideHours;

  factory ConfigBounds.fromJson(Map<String, dynamic> json) {
    return ConfigBounds(
      clampMin: asDouble(json['clamp_min']),
      clampMax: asDouble(json['clamp_max']),
      overrideHours: asInt(json['override_hours'], 0),
    );
  }
}
