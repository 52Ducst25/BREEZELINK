import '../models/telemetry_sample.dart';
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
}
