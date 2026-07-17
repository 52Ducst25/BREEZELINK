import 'ac_mode.dart';
import 'parse_utils.dart';

/// Mirrors backend `ComfortLogRead` (`schemas/comfort.py`) — one audit row
/// for the decision-history chart ("why was this setpoint chosen").
class ComfortLogEntry {
  const ComfortLogEntry({
    required this.ts,
    required this.tOut,
    required this.hOut,
    required this.tIn,
    required this.hIn,
    required this.tRm,
    required this.tNeutral,
    required this.humidPenalty,
    required this.tTarget,
    required this.tSet,
    required this.mode,
  });

  final DateTime ts;
  final double tOut;
  final double? hOut;
  final double tIn;
  final double hIn;
  final double tRm;
  final double tNeutral;
  final double humidPenalty;
  final double tTarget;
  final int tSet;
  final AcMode? mode;

  factory ComfortLogEntry.fromJson(Map<String, dynamic> json) {
    return ComfortLogEntry(
      ts: asDateTimeOrNull(json['ts']) ?? DateTime.now(),
      tOut: asDouble(json['t_out']),
      hOut: asDoubleOrNull(json['h_out']),
      tIn: asDouble(json['t_in']),
      hIn: asDouble(json['h_in']),
      tRm: asDouble(json['t_rm']),
      tNeutral: asDouble(json['t_neutral']),
      humidPenalty: asDouble(json['humid_penalty']),
      tTarget: asDouble(json['t_target']),
      tSet: asInt(json['t_set']),
      mode: acModeFromWire(asStringOrNull(json['mode'])),
    );
  }
}
