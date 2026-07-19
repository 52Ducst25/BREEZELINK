import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../screens/auth/login_screen.dart';
import '../services/api_client.dart';
import '../services/auth_api.dart';
import '../services/comfort_api.dart';
import '../services/config_api.dart';
import '../services/credential_store.dart';
import '../services/device_api.dart';
import '../services/ir_api.dart';
import '../services/telemetry_api.dart';
import '../state/app_state.dart';
import '../widgets/update_prompt.dart';
import 'main_shell.dart';

const _kBaseUrlKey = 'base_url';
const _kDefaultBaseUrl = 'https://admin.vi-du.com';

/// Login gate: unauthenticated -> [LoginScreen]; authenticated -> builds the
/// [ApiClient] + domain wrappers + [AppState] and shows [MainShell]. Ported
/// structure from SafeKitchen's `_AuthGate` — always requires a fresh login
/// on launch (only email/password are optionally remembered for
/// convenience), rather than silently restoring a stored session.
class AuthGate extends StatefulWidget {
  const AuthGate({super.key, required this.prefs});
  final SharedPreferences prefs;

  @override
  State<AuthGate> createState() => _AuthGateState();
}

class _AuthGateState extends State<AuthGate> {
  AppState? _appState;

  /// Base URL to run the update check against once the app shell is on screen,
  /// then cleared. Set by [_enter]; consumed by [build]'s post-frame callback.
  ///
  /// Deferred rather than run inside [_enter] because the dialog needs a
  /// context that sits under a Navigator — during _enter we are still showing
  /// the login route and about to replace it. Checking AFTER login also means
  /// the prompt never blocks someone from signing in, and we only nag people
  /// who actually use the app.
  String? _pendingUpdateCheck;

  Future<String?> _login(String baseUrl, String email, String password, bool remember) async {
    final client = ApiClient(baseUrl);
    final authApi = AuthApi(client);
    final err = await authApi.login(email, password);
    if (err != null) return err;

    if (remember) {
      await CredentialStore.saveAccount(email, password);
    } else {
      await CredentialStore.removeAccount(email);
    }
    await _enter(client, authApi, baseUrl);
    return null;
  }

  /// Ask the server to e-mail a reset link. Returns null on acceptance; the
  /// server answers 202 whether or not the e-mail exists, so a null here means
  /// only "request accepted", never "that e-mail is registered".
  Future<String?> _forgot(String baseUrl, String email) async {
    return AuthApi(ApiClient(baseUrl)).forgotPassword(email);
  }

  /// Activate with a vendor-issued code, then drop straight into the app.
  ///
  /// Does NOT log the user straight in: the register screen shows a success
  /// confirmation and returns to login (product decision — the customer sees
  /// "account created" and signs in deliberately). The new account is saved so
  /// it appears pre-filled in the login screen's saved-accounts picker.
  Future<String?> _register(
    String baseUrl,
    String code,
    String email,
    String password,
    String fullName,
    String phone,
  ) async {
    final err = await AuthApi(ApiClient(baseUrl)).register(
      code: code,
      email: email,
      password: password,
      fullName: fullName,
      phone: phone,
    );
    if (err != null) return err;
    await widget.prefs.setString(_kBaseUrlKey, baseUrl);
    await CredentialStore.saveAccount(email, password);
    return null;
  }

  /// Shared tail of login/register: persist the server URL and stand up the
  /// session. Both paths arrive here with an [ApiClient] that already holds a
  /// token pair, so the wiring is identical and belongs in one place.
  Future<void> _enter(ApiClient client, AuthApi authApi, String baseUrl) async {
    await widget.prefs.setString(_kBaseUrlKey, baseUrl);
    _pendingUpdateCheck = baseUrl;
    final appState = AppState(
      apiClient: client,
      authApi: authApi,
      comfortApi: ComfortApi(client),
      deviceApi: DeviceApi(client),
      telemetryApi: TelemetryApi(client),
      irApi: IrApi(client),
      configApi: ConfigApi(client),
    );
    client.onSessionExpired = _forceLogout;
    appState.start();
    if (mounted) setState(() => _appState = appState);
  }

  void _forceLogout() {
    final s = _appState;
    if (s == null) return;
    s.dispose();
    if (mounted) setState(() => _appState = null);
  }

  @override
  void dispose() {
    _appState?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final appState = _appState;
    if (appState == null) {
      final baseUrl = widget.prefs.getString(_kBaseUrlKey) ?? _kDefaultBaseUrl;
      // The login screen owns register/forgot navigation now, so it can refresh
      // its saved-accounts picker after a registration returns.
      return LoginScreen(
        initialBaseUrl: baseUrl,
        onLogin: _login,
        onRegister: _register,
        onForgot: _forgot,
      );
    }
    // Run the update check once, after MainShell's first frame — by then the
    // dialog has a Navigator to attach to. Cleared immediately so a rebuild
    // (any setState, any theme change) cannot re-prompt.
    final pending = _pendingUpdateCheck;
    if (pending != null) {
      _pendingUpdateCheck = null;
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) maybePromptUpdate(context, pending);
      });
    }

    return ChangeNotifierProvider.value(
      value: appState,
      child: MainShell(onLogout: _forceLogout),
    );
  }
}
