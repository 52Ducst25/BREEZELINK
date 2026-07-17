import 'ac_mode.dart';
import 'parse_utils.dart';

/// Mirrors backend `ComfortPreview` (`schemas/comfort.py`) exactly, including
/// nullability. CRITICAL: every numeric field here stays nullable end-to-end —
/// no `?? 0` default anywhere in this model or its parsing. The predecessor
/// app defaulted missing readings to 0.0 and rendered a fabricated 30.5degC
/// setpoint at the same time it showed "no data"; this shape makes that bug
/// structurally impossible because callers are forced to handle null.
///
/// Pipeline order (see brief): t_rm -> t_neutral -> humid_penalty -> t_target
/// -> t_set (snapped to a learned IR code) -> mode.
class ComfortPreview {
  const ComfortPreview({
    required this.dataAvailable,
    required this.tRm,
    required this.tNeutral,
    required this.humidPenalty,
    required this.tTarget,
    required this.tSet,
    required this.mode,
    required this.stale,
    required this.reason,
  });

  final bool dataAvailable;
  final double? tRm;
  final double? tNeutral;
  final double? humidPenalty;
  final double? tTarget;

  /// Discrete setpoint snapped to a learned IR code. Null while no code has
  /// been LEARNed yet (auto-control gate) — render "—", never 0.
  final int? tSet;

  /// Decided mode. Null under the same IR-gate condition as [tSet].
  final AcMode? mode;

  /// True if outdoor telemetry was stale this cycle (UI should flag it).
  final bool stale;

  /// Human-readable explanation of why this decision (or lack of one) holds —
  /// always render this so "no data"/"gated" states are self-explanatory.
  final String reason;

  factory ComfortPreview.fromJson(Map<String, dynamic> json) {
    return ComfortPreview(
      dataAvailable: asBool(json['data_available'], true),
      tRm: asDoubleOrNull(json['t_rm']),
      tNeutral: asDoubleOrNull(json['t_neutral']),
      humidPenalty: asDoubleOrNull(json['humid_penalty']),
      tTarget: asDoubleOrNull(json['t_target']),
      tSet: asIntOrNull(json['t_set']),
      mode: acModeFromWire(asStringOrNull(json['mode'])),
      stale: asBool(json['stale']),
      reason: asString(json['reason'], ''),
    );
  }

  /// Placeholder for "no snapshot received yet" (before first WS/poll response) —
  /// distinct from a server-confirmed `data_available:false`, but rendered the
  /// same honest way (em-dashes) by the UI either way.
  static const loading = ComfortPreview(
    dataAvailable: false,
    tRm: null,
    tNeutral: null,
    humidPenalty: null,
    tTarget: null,
    tSet: null,
    mode: null,
    stale: false,
    reason: 'Đang tải dữ liệu…',
  );
}
