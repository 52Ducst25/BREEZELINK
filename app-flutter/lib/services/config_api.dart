import '../models/config_bounds.dart';
import 'api_client.dart';
import 'api_exception.dart';

/// `/configs` — OWNER-ONLY (see `api/v1/config_routes.py::require_role("owner")`).
/// A `member` caller gets 403; this wrapper returns null rather than
/// surfacing that as an error, because "I can't see the bounds" is an
/// expected state for that role, not a failure (brief rule: don't guess a
/// fallback range when the real bounds are unreadable).
class ConfigApi {
  ConfigApi(this._client);
  final ApiClient _client;

  Future<ConfigBounds?> bounds() async {
    try {
      final body = await _client.get('/api/v1/configs');
      return ConfigBounds.fromJson(body);
    } on ApiException {
      return null;
    }
  }
}
