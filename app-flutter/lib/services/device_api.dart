import '../models/device.dart';
import 'api_client.dart';

/// `/devices` — list is any org member; registering a new physical node is
/// owner-only (not exposed in this app — devices are provisioned via the
/// firmware pairing flow, out of scope per the brief).
class DeviceApi {
  DeviceApi(this._client);
  final ApiClient _client;

  Future<List<Device>> list() async {
    final rows = await _client.getList('/api/v1/devices');
    return rows.map((e) => Device.fromJson(e as Map<String, dynamic>)).toList();
  }
}
