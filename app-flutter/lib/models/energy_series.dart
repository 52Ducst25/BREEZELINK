import 'parse_utils.dart';

/// Mirrors backend `GET /api/v1/energy/series`. A time-ordered list of watt
/// samples plus an optional total kWh for the window. Both may be empty/null —
/// the chart shows "chưa có dữ liệu" rather than an empty axis of zeros.
class EnergySeries {
  const EnergySeries({required this.wattSeries, required this.totalKwh});

  final List<WattPoint> wattSeries;
  final double? totalKwh;

  bool get isEmpty => wattSeries.isEmpty;

  factory EnergySeries.fromJson(Map<String, dynamic> json) {
    final rows = (json['watt_series'] as List?) ?? const [];
    final points = <WattPoint>[];
    for (final r in rows) {
      if (r is Map<String, dynamic>) {
        final p = WattPoint.fromJsonOrNull(r);
        if (p != null) points.add(p);
      }
    }
    return EnergySeries(
      wattSeries: points,
      totalKwh: asDoubleOrNull(json['total_kwh']),
    );
  }

  static const empty = EnergySeries(wattSeries: [], totalKwh: null);
}

/// One {ts, watt} point on the power line. Skipped entirely when either field
/// is unparseable — never coerced to a fake 0 W.
class WattPoint {
  const WattPoint({required this.ts, required this.watt});

  final DateTime ts;
  final double watt;

  static WattPoint? fromJsonOrNull(Map<String, dynamic> json) {
    final ts = asDateTimeOrNull(json['ts']);
    final watt = asDoubleOrNull(json['watt']);
    if (ts == null || watt == null) return null;
    return WattPoint(ts: ts, watt: watt);
  }
}
