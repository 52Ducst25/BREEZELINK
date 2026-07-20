import '../models/energy_overview.dart';
import '../models/energy_series.dart';
import 'api_client.dart';
import 'api_exception.dart';

/// `/energy/*` — power consumption. Any authenticated org member may read.
///
/// Both calls swallow [ApiException] and return null / empty: the endpoints may
/// not exist yet (backend is a parallel workstream) or the meter may simply
/// have no data. Callers render "Chưa có dữ liệu điện" for those states rather
/// than surfacing an error or fabricating numbers.
class EnergyApi {
  EnergyApi(this._client);
  final ApiClient _client;

  Future<EnergyOverview?> overview() async {
    try {
      final body = await _client.get('/api/v1/energy/overview');
      return EnergyOverview.fromJson(body);
    } on ApiException {
      return null;
    }
  }

  /// Watt series for a window. [deviceId] null asks for the household total.
  Future<EnergySeries> series({
    String? deviceId,
    required DateTime start,
    required DateTime end,
  }) async {
    try {
      final body = await _client.get('/api/v1/energy/series', query: {
        'device_id': ?deviceId,
        'start': start.toUtc().toIso8601String(),
        'end': end.toUtc().toIso8601String(),
      });
      return EnergySeries.fromJson(body);
    } on ApiException {
      return EnergySeries.empty;
    }
  }
}
