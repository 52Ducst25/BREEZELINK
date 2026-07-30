import 'dart:async';

import '../models/ac_state.dart';
import '../models/comfort_preview.dart';
import '../models/device.dart';
import '../models/live_reading.dart';
import 'api_client.dart';
import 'comfort_api.dart';
import 'device_api.dart';
import 'ws_client.dart';

/// Belt-and-braces realtime source: the WS feed pushes on every change, and a
/// slow poll re-fetches independently as a safety net in case the socket is
/// silently dead behind a proxy (mirrors SafeKitchen's dual poll+WS design).
/// Whichever arrives, freshest wins — both write into the same broadcast
/// streams that [AppState] listens to.
class LiveDataSource {
  LiveDataSource({
    required ApiClient apiClient,
    required this._comfortApi,
    required this._deviceApi,
  }) {
    _ws = WsClient(
      baseUrl: apiClient.baseUrl,
      getAccessToken: () => apiClient.currentAccessToken,
      onSnapshot: _onSnapshot,
      onConnectedChanged: (c) => _wsConnected.add(c),
    );
  }

  final ComfortApi _comfortApi;
  final DeviceApi _deviceApi;
  late final WsClient _ws;
  Timer? _pollTimer;

  final _comfort = StreamController<ComfortPreview>.broadcast();
  final _indoor = StreamController<LiveReading?>.broadcast();
  final _outdoor = StreamController<LiveReading?>.broadcast();
  final _ac = StreamController<AcState?>.broadcast();
  final _devices = StreamController<List<Device>>.broadcast();
  final _wsConnected = StreamController<bool>.broadcast();

  Stream<ComfortPreview> get comfort => _comfort.stream;
  Stream<LiveReading?> get indoor => _indoor.stream;
  Stream<LiveReading?> get outdoor => _outdoor.stream;

  /// Máy lạnh ĐANG được đặt ở mức nào. Chỉ tới qua WS, giống `indoor`/`outdoor`
  /// — `_poll()` không làm mới được vì `build_live_state` chỉ có đường WebSocket,
  /// không có endpoint REST tương ứng. Nên khi socket chết sau proxy, giá trị này
  /// đứng lại ở ảnh chụp cuối thay vì tự cũ đi một cách âm thầm.
  Stream<AcState?> get ac => _ac.stream;
  Stream<List<Device>> get devices => _devices.stream;
  Stream<bool> get wsConnected => _wsConnected.stream;

  void start() {
    _ws.connect();
    _poll(); // immediate first fetch — don't wait for the first tick
    _pollTimer = Timer.periodic(const Duration(seconds: 20), (_) => _poll());
  }

  void _onSnapshot(Map<String, dynamic> snapshot) {
    final comfortJson = snapshot['comfort'];
    if (comfortJson is Map) {
      _comfort.add(
        ComfortPreview.fromJson(comfortJson.cast<String, dynamic>()),
      );
    }
    _indoor.add(LiveReading.fromJsonOrNull(snapshot['indoor']));
    _outdoor.add(LiveReading.fromJsonOrNull(snapshot['outdoor']));
    _ac.add(AcState.fromJsonOrNull(snapshot['ac']));
    final devicesJson = snapshot['devices'];
    if (devicesJson is List) {
      _devices.add(
        devicesJson
            .whereType<Map>()
            .map((e) => Device.fromJson(e.cast<String, dynamic>()))
            .toList(),
      );
    }
  }

  Future<void> _poll() async {
    try {
      _comfort.add(await _comfortApi.preview());
    } catch (_) {
      // Swallow — WS or the next poll tick will carry the next good snapshot.
    }
    try {
      _devices.add(await _deviceApi.list());
    } catch (_) {}
  }

  void dispose() {
    _pollTimer?.cancel();
    _ws.dispose();
    _comfort.close();
    _indoor.close();
    _outdoor.close();
    _ac.close();
    _devices.close();
    _wsConnected.close();
  }
}
