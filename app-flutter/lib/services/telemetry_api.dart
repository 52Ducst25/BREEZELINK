import '../models/telemetry_sample.dart';
import '../models/telemetry_series.dart';
import 'api_exception.dart';
import 'api_client.dart';

/// `/telemetry?device_id=` — chart series for one device.
class TelemetryApi {
  TelemetryApi(this._client);
  final ApiClient _client;

  Future<List<TelemetrySample>> list(String deviceId, {int limit = 200}) async {
    final rows = await _client.getList('/api/v1/telemetry', query: {
      'device_id': deviceId,
      'limit': limit,
    });
    return rows.map((e) => TelemetrySample.fromJson(e as Map<String, dynamic>)).toList();
  }

  /// Down-sampled history for the per-device chart screen. The backend averages
  /// each of `buckets` equal slots, so a 30-day window really spans 30 days
  /// (the raw `list` endpoint would return only the newest rows instead).
  ///
  /// Returns an EMPTY series on ANY failure rather than throwing: the history
  /// screen shows "chưa có dữ liệu", which is honest, instead of an error box
  /// on a screen whose whole job is to render whatever data exists. Catching
  /// broadly (not just [ApiException]) matters — a parse/transport error that
  /// escaped here previously left the screen spinning forever.
  Future<SensorSeries> series({
    required String deviceId,
    required DateTime start,
    required DateTime end,
    int buckets = 120,
  }) async {
    try {
      final rows = await _client.getList('/api/v1/telemetry/series', query: {
        'device_id': deviceId,
        'start': start.toUtc().toIso8601String(),
        'end': end.toUtc().toIso8601String(),
        'buckets': buckets,
      });
      return SensorSeries(
        rows.map((e) => SensorPoint.fromJson(e as Map<String, dynamic>)).toList(),
      );
    } catch (_) {
      return SensorSeries.empty;
    }
  }
}
