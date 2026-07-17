import '../models/ac_mode.dart';
import '../models/ir_code.dart';
import 'api_client.dart';

/// `/ir/*` — LEARN flow: trigger capture, then poll [coverage] until the
/// captured code shows up (the server responds 202 fire-and-forget; the
/// actual code arrives later over MQTT, not in the POST response).
class IrApi {
  IrApi(this._client);
  final ApiClient _client;

  Future<void> triggerLearn({required AcMode mode, int? temp}) => _client.post('/api/v1/ir/learn', data: {
        'mode': mode.wireValue,
        if (temp != null) 'temp': temp,
      });

  Future<IrCoverage> coverage() async {
    final body = await _client.get('/api/v1/ir/codes');
    return IrCoverage.fromJson(body);
  }
}
