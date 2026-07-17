import 'parse_utils.dart';

/// Mirrors backend `TelemetryRead` (`schemas/telemetry.py`) — one chart point.
class TelemetrySample {
  const TelemetrySample({
    required this.deviceId,
    required this.ts,
    required this.temp,
    required this.humidity,
    required this.rssi,
    required this.batt,
  });

  final String deviceId;
  final DateTime ts;
  final double temp;
  final double humidity;
  final int rssi;
  final double? batt;

  factory TelemetrySample.fromJson(Map<String, dynamic> json) {
    return TelemetrySample(
      deviceId: asString(json['device_id']),
      ts: asDateTimeOrNull(json['ts']) ?? DateTime.now(),
      temp: asDouble(json['temp']),
      humidity: asDouble(json['humidity']),
      rssi: asInt(json['rssi']),
      batt: asDoubleOrNull(json['batt']),
    );
  }
}
