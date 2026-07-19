import 'ac_mode.dart';
import 'parse_utils.dart';

/// Mirrors backend `IrCodeRead` (`schemas/ir.py`) — one captured remote button.
class IrCode {
  const IrCode({
    required this.id,
    required this.mode,
    required this.temp,
    required this.brand,
    required this.buttonLabel,
  });

  final String id;
  final AcMode? mode;
  final int temp;
  final String? brand;
  final String buttonLabel;

  factory IrCode.fromJson(Map<String, dynamic> json) {
    return IrCode(
      id: asString(json['id']),
      mode: acModeFromWire(asStringOrNull(json['mode'])),
      temp: asInt(json['temp']),
      brand: asStringOrNull(json['brand']),
      buttonLabel: asString(json['button_label'], '—'),
    );
  }
}

/// Mirrors backend `IrCoverageResponse` — captured codes + still-missing
/// buttons (design: COOL 24-28, DRY, FAN, OFF must all exist before
/// auto-control is safe).
class IrCoverage {
  const IrCoverage({required this.codes, required this.missing, this.actions = const []});

  final List<IrCode> codes;
  final List<String> missing;

  /// Learned standalone action buttons, e.g. `["FAN_SPEED"]`. Not part of the
  /// required-coverage matrix.
  final List<String> actions;

  bool get isComplete => missing.isEmpty;
  bool get hasFanSpeed => actions.contains('FAN_SPEED');

  factory IrCoverage.fromJson(Map<String, dynamic> json) {
    final codesJson = (json['codes'] as List?) ?? const [];
    final missingJson = (json['missing'] as List?) ?? const [];
    final actionsJson = (json['actions'] as List?) ?? const [];
    return IrCoverage(
      codes: codesJson.map((e) => IrCode.fromJson(e as Map<String, dynamic>)).toList(),
      missing: missingJson.map((e) => e.toString()).toList(),
      actions: actionsJson.map((e) => e.toString()).toList(),
    );
  }
}
