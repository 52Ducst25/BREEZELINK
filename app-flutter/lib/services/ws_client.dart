import 'dart:async';
import 'dart:convert';

import 'package:web_socket_channel/web_socket_channel.dart';

/// Realtime feed client for `GET /api/v1/ws/live?token=<access_jwt>` — the
/// backend pushes a full `{type:"state", comfort, indoor, outdoor, devices}`
/// snapshot on every change, plus `{"type":"ping"}` every ~25s (heartbeat,
/// also keeps the Cloudflare tunnel alive). This client ignores pings,
/// forwards state snapshots via [onSnapshot], and auto-reconnects with a
/// fixed backoff on any close/error (org-scoped single socket — no per-device
/// URL like SafeKitchen's `/ws/device/{id}`).
class WsClient {
  WsClient({
    required this.baseUrl,
    required this.getAccessToken,
    required this.onSnapshot,
    required this.onConnectedChanged,
  });

  final String baseUrl;

  /// Read fresh each (re)connect attempt — the access token can rotate via
  /// [ApiClient]'s refresh flow between reconnects.
  final String? Function() getAccessToken;
  final void Function(Map<String, dynamic> snapshot) onSnapshot;
  final void Function(bool connected) onConnectedChanged;

  WebSocketChannel? _channel;
  StreamSubscription? _sub;
  Timer? _reconnectTimer;
  bool _disposed = false;

  void connect() {
    if (_disposed) return;
    final token = getAccessToken();
    if (token == null) {
      _scheduleReconnect();
      return;
    }
    final uri = _wsUri(token);
    try {
      final channel = WebSocketChannel.connect(uri);
      _channel = channel;
      _sub = channel.stream.listen(
        _onData,
        onDone: _onClosed,
        onError: (_) => _onClosed(),
        cancelOnError: true,
      );
      onConnectedChanged(true);
    } catch (_) {
      _onClosed();
    }
  }

  Uri _wsUri(String token) {
    var b = baseUrl.endsWith('/') ? baseUrl.substring(0, baseUrl.length - 1) : baseUrl;
    b = b.replaceFirst(RegExp(r'^http'), 'ws'); // http->ws, https->wss
    return Uri.parse('$b/api/v1/ws/live?token=$token');
  }

  void _onData(dynamic raw) {
    try {
      final msg = jsonDecode(raw as String);
      if (msg is! Map) return;
      if (msg['type'] == 'ping') return; // heartbeat only — nothing to render
      if (msg['type'] == 'state') onSnapshot(msg.cast<String, dynamic>());
    } catch (_) {
      // Malformed frame — drop it, keep the connection (next frame may be fine).
    }
  }

  void _onClosed() {
    _sub?.cancel();
    _sub = null;
    _channel = null;
    onConnectedChanged(false);
    _scheduleReconnect();
  }

  void _scheduleReconnect() {
    if (_disposed) return;
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 4), connect);
  }

  void dispose() {
    _disposed = true;
    _reconnectTimer?.cancel();
    _sub?.cancel();
    _channel?.sink.close();
  }
}
