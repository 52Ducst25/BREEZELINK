import 'package:flutter/material.dart';

/// AC operating mode — mirrors backend `AcMode` (`app.models.enums.AcMode`).
/// Kept as a plain enum with explicit wire-value mapping (not `.name`) so a
/// backend rename of the Dart enum member does not silently break parsing.
enum AcMode { cool, dry, fan, off }

extension AcModeWire on AcMode {
  String get wireValue => switch (this) {
        AcMode.cool => 'COOL',
        AcMode.dry => 'DRY',
        AcMode.fan => 'FAN',
        AcMode.off => 'OFF',
      };

  /// Vietnamese label for UI display.
  String get label => switch (this) {
        AcMode.cool => 'LÀM LẠNH',
        AcMode.dry => 'HÚT ẨM',
        AcMode.fan => 'QUẠT GIÓ',
        AcMode.off => 'TẮT',
      };

  IconData get icon => switch (this) {
        AcMode.cool => Icons.ac_unit,
        AcMode.dry => Icons.water_drop_outlined,
        AcMode.fan => Icons.air,
        AcMode.off => Icons.power_settings_new,
      };
}

/// Parses the backend's uppercase wire value; returns null on anything else
/// so callers can treat "unrecognized mode" as absence, not a guess.
AcMode? acModeFromWire(String? v) {
  switch (v) {
    case 'COOL':
      return AcMode.cool;
    case 'DRY':
      return AcMode.dry;
    case 'FAN':
      return AcMode.fan;
    case 'OFF':
      return AcMode.off;
    default:
      return null;
  }
}
