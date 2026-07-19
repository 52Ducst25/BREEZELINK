import 'dart:async';

import 'package:flutter/foundation.dart';

import '../models/ac_mode.dart';
import '../models/comfort_preview.dart';
import '../models/config_bounds.dart';
import '../models/device.dart';
import '../models/live_reading.dart';
import '../models/user_profile.dart';
import '../services/api_client.dart';
import '../services/api_exception.dart';
import '../services/auth_api.dart';
import '../services/comfort_api.dart';
import '../services/config_api.dart';
import '../services/device_api.dart';
import '../services/ir_api.dart';
import '../services/live_data_source.dart';
import '../services/telemetry_api.dart';

/// Central app state: aggregates the realtime comfort/indoor/outdoor/devices
/// snapshot (WS + poll, see [LiveDataSource]) plus the caller's profile and
/// (role-permitting) the real server setpoint bounds. UI reads this via
/// `context.watch<AppState>()`; domain API wrappers stay reachable for
/// screen-specific calls (telemetry history, IR learn) that don't need to
/// live in shared state.
class AppState extends ChangeNotifier {
  AppState({
    required this.apiClient,
    required this.authApi,
    required this.comfortApi,
    required this.deviceApi,
    required this.telemetryApi,
    required this.irApi,
    required this.configApi,
  }) {
    _live = LiveDataSource(apiClient: apiClient, comfortApi: comfortApi, deviceApi: deviceApi);
  }

  final ApiClient apiClient;
  final AuthApi authApi;
  final ComfortApi comfortApi;
  final DeviceApi deviceApi;
  final TelemetryApi telemetryApi;
  final IrApi irApi;
  final ConfigApi configApi;

  late final LiveDataSource _live;
  final _subs = <StreamSubscription>[];

  ComfortPreview _comfort = ComfortPreview.loading;
  LiveReading? _indoor;
  LiveReading? _outdoor;
  List<Device> _devices = const [];
  bool _wsConnected = false;
  UserProfile? _profile;

  /// Real server-side setpoint clamp bounds — null until loaded, and stays
  /// null forever for a `member` (owner-only endpoint, 403 for others). The
  /// override UI must show that honestly rather than guessing a range.
  ConfigBounds? _bounds;

  ComfortPreview get comfort => _comfort;
  LiveReading? get indoor => _indoor;
  LiveReading? get outdoor => _outdoor;
  List<Device> get devices => _devices;
  bool get wsConnected => _wsConnected;
  UserProfile? get profile => _profile;
  ConfigBounds? get bounds => _bounds;

  void start() {
    _subs.add(_live.comfort.listen((c) {
      _comfort = c;
      notifyListeners();
    }));
    _subs.add(_live.indoor.listen((r) {
      _indoor = r;
      notifyListeners();
    }));
    _subs.add(_live.outdoor.listen((r) {
      _outdoor = r;
      notifyListeners();
    }));
    _subs.add(_live.devices.listen((d) {
      _devices = d;
      notifyListeners();
    }));
    _subs.add(_live.wsConnected.listen((c) {
      _wsConnected = c;
      notifyListeners();
    }));
    _live.start();
    unawaited(_loadProfile());
    unawaited(_loadBounds());
  }

  Future<void> _loadProfile() async {
    _profile = await authApi.me();
    notifyListeners();
  }

  Future<void> _loadBounds() async {
    _bounds = await configApi.bounds(); // silently null for non-owner — by design
    notifyListeners();
  }

  /// Sets a manual override. Returns null on success, else a Vietnamese
  /// message — including the non-fatal "missing IR codes" warning the
  /// server returns alongside a 200 (override still applies).
  Future<String?> setOverride({required AcMode mode, required int setpoint}) async {
    try {
      final missing = await comfortApi.setOverride(mode: mode, setpoint: setpoint);
      if (missing.isNotEmpty) {
        return 'Đã đặt ghi đè, nhưng còn thiếu mã lệnh IR: ${missing.join(', ')}';
      }
      return null;
    } on ApiException catch (e) {
      return e.message;
    }
  }

  Future<String?> clearOverride() async {
    try {
      await comfortApi.clearOverride();
      return null;
    } on ApiException catch (e) {
      return e.message;
    }
  }

  /// Presses one learned remote button. Returns null on success; a Vietnamese
  /// message when that button hasn't been learned yet, or on request failure.
  Future<String?> sendAction(String action) async {
    try {
      final r = await comfortApi.sendAction(action);
      return r.available ? null : r.detail;
    } on ApiException catch (e) {
      return e.message;
    }
  }

  @override
  void dispose() {
    for (final s in _subs) {
      s.cancel();
    }
    _live.dispose();
    super.dispose();
  }
}
