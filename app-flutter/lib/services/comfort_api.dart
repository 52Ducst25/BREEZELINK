import '../models/ac_mode.dart';
import '../models/comfort_log_entry.dart';
import '../models/comfort_preview.dart';
import 'api_client.dart';
import 'api_exception.dart';

/// `/comfort/*` calls: read-only preview, manual override set/clear, decision
/// history. Any authenticated org member may call these (no owner gate).
class ComfortApi {
  ComfortApi(this._client);
  final ApiClient _client;

  Future<ComfortPreview> preview() async {
    final body = await _client.get('/api/v1/comfort/preview');
    return ComfortPreview.fromJson(body);
  }

  /// Throws [ApiException] with the server's Vietnamese-translated reason on
  /// rejection (e.g. setpoint outside the real server-side clamp bounds).
  Future<List<String>> setOverride({required AcMode mode, required int setpoint}) async {
    final body = await _client.post('/api/v1/comfort/override', data: {
      'mode': mode.wireValue,
      'setpoint': setpoint,
    });
    final missing = body['missing_ir_codes'];
    return missing is List ? missing.map((e) => e.toString()).toList() : const [];
  }

  Future<void> clearOverride() => _client.delete('/api/v1/comfort/override');

  /// Press one learned remote button (any `kAcActions` wire code — FAN_SPEED,
  /// SLEEP, ECO, …). `available` is false when that button hasn't been learned
  /// yet; the server returns that as a normal 200, not an error.
  Future<({bool available, String detail})> sendAction(String action) async {
    final body = await _client.post('/api/v1/comfort/ir-action', data: {'action': action});
    return (
      available: body['available'] == true,
      detail: (body['detail'] ?? '').toString(),
    );
  }

  Future<List<ComfortLogEntry>> log({int limit = 100}) async {
    final rows = await _client.getList('/api/v1/comfort/log', query: {'limit': limit});
    return rows.map((e) => ComfortLogEntry.fromJson(e as Map<String, dynamic>)).toList();
  }
}
