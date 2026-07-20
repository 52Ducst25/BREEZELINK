import 'parse_utils.dart';

/// One down-sampled point of a device's history chart — mirrors the backend's
/// `GET /api/v1/telemetry/series` rows (bucket timestamp + averaged readings).
class SensorPoint {
  const SensorPoint({required this.ts, required this.temp, required this.humidity});

  final DateTime ts;
  final double temp;
  final double humidity;

  factory SensorPoint.fromJson(Map<String, dynamic> json) => SensorPoint(
        ts: asDateTimeOrNull(json['ts']) ?? DateTime.now(),
        temp: asDouble(json['temp']),
        humidity: asDouble(json['humidity']),
      );
}

/// The series for one device over one window. Empty means "the backend has no
/// readings in this range" — the UI must say so rather than draw a flat zero.
class SensorSeries {
  const SensorSeries(this.points);

  final List<SensorPoint> points;

  static const SensorSeries empty = SensorSeries(<SensorPoint>[]);

  bool get isEmpty => points.length < 2; // a single point cannot draw a line

  /// Min/max across the window, for the summary row above the charts. Null
  /// when there is nothing to summarise.
  ({double min, double max})? range(double Function(SensorPoint) pick) {
    if (points.isEmpty) return null;
    var lo = pick(points.first), hi = lo;
    for (final p in points) {
      final v = pick(p);
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    return (min: lo, max: hi);
  }
}
