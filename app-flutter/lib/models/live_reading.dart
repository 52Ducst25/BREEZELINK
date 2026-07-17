import 'parse_utils.dart';

/// One indoor/outdoor {t, h} reading as pushed by the WS `state` snapshot
/// (`live_state_service._reading`) or absent entirely (`null` — no telemetry
/// yet). Never fabricate a reading when the server sends null.
class LiveReading {
  const LiveReading({required this.temp, required this.humidity});

  final double temp;
  final double humidity;

  static LiveReading? fromJsonOrNull(dynamic json) {
    if (json is! Map) return null;
    final t = asDoubleOrNull(json['t']);
    final h = asDoubleOrNull(json['h']);
    if (t == null || h == null) return null;
    return LiveReading(temp: t, humidity: h);
  }
}
