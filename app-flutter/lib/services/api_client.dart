import 'dart:async';

import 'package:dio/dio.dart';

import 'api_exception.dart';

/// Core REST client: owns the Dio instance, the JWT pair, and the
/// refresh-on-401 flow. Domain calls (auth/comfort/devices/...) live in
/// their own `*_api.dart` wrappers and call [get]/[post]/[put]/[delete] here
/// — keeps this file DRY (one place owns auth headers + refresh + the
/// body-shape guard) instead of every wrapper re-implementing it.
class ApiClient {
  /// This build's version, e.g. "1.0.0+1". Resolved once from the installed
  /// package in `main()` before `runApp` and read here.
  ///
  /// A static rather than a constructor parameter because [ApiClient] is built
  /// synchronously inside an initializer list (below) and cannot await
  /// PackageInfo, and because both construction sites live in auth_gate —
  /// threading it through them would buy nothing.
  static String appVersion = '';

  ApiClient(this.baseUrl) : _dio = Dio(BaseOptions(
          baseUrl: baseUrl,
          connectTimeout: const Duration(seconds: 10),
          receiveTimeout: const Duration(seconds: 10),
          // Set ONCE here, so every call carries it: login, register, refresh
          // and every domain wrapper share this single Dio. Dio MERGES
          // per-request Options.headers into these rather than replacing them,
          // so the Authorization header added below does not clobber it, and
          // the 401-retry path reuses already-merged headers.
          //
          // A header, not a body field: LoginRequest is a plain pydantic model
          // and silently DROPS unknown fields, so an app_version in the JSON
          // would vanish without an error. The server reads it off the request
          // in auth_routes.login.
          headers: {'X-App-Version': appVersion},
        )) {
    _dio.interceptors.add(InterceptorsWrapper(onError: _onError));
  }

  final String baseUrl;
  final Dio _dio;

  String? _accessToken;
  String? _refreshToken;

  /// One in-flight refresh at a time — concurrent 401s await the same future
  /// instead of each firing their own `/auth/refresh` call.
  Future<bool>? _refreshing;

  /// Called when the refresh token is also rejected — the caller (AuthGate)
  /// must drop back to the login screen; this client cannot recover alone.
  VoidCallback? onSessionExpired;

  bool get isLoggedIn => _accessToken != null;

  /// Exposed narrowly for [WsClient] reconnects, which need the CURRENT token
  /// (it rotates via the refresh flow) — not for general use, prefer the
  /// `get`/`post`/... helpers which attach it automatically.
  String? get currentAccessToken => _accessToken;

  void setTokens({required String access, String? refresh}) {
    _accessToken = access;
    if (refresh != null) _refreshToken = refresh;
  }

  void clearTokens() {
    _accessToken = null;
    _refreshToken = null;
  }

  Options get _auth => Options(headers: {'Authorization': 'Bearer $_accessToken'});

  Future<Map<String, dynamic>> get(String path, {Map<String, dynamic>? query}) =>
      _send(() => _dio.get(path, queryParameters: query, options: _auth));

  Future<List<dynamic>> getList(String path, {Map<String, dynamic>? query}) async {
    final r = await _dio.get(path, queryParameters: query, options: _auth);
    return asListOrThrow(r.data);
  }

  Future<Map<String, dynamic>> post(String path, {Object? data, Map<String, dynamic>? query}) =>
      _send(() => _dio.post(path, data: data, queryParameters: query, options: _auth));

  Future<Map<String, dynamic>> put(String path, {Object? data}) =>
      _send(() => _dio.put(path, data: data, options: _auth));

  Future<Map<String, dynamic>> delete(String path) => _send(() => _dio.delete(path, options: _auth));

  /// Unauthenticated call (login/register/forgot-password) — no Bearer header.
  Future<Map<String, dynamic>> postPublic(String path, {Object? data}) async {
    try {
      final r = await _dio.post(path, data: data);
      return asMapOrThrow(r.data);
    } on DioException catch (e) {
      throw ApiException(translateDioError(e, baseUrl: baseUrl));
    }
  }

  Future<Map<String, dynamic>> _send(Future<Response> Function() call) async {
    try {
      final r = await call();
      return asMapOrThrow(r.data);
    } on DioException catch (e) {
      throw ApiException(translateDioError(e, baseUrl: baseUrl));
    }
  }

  /// Refresh-on-401: a request that failed with 401 (and isn't the login/
  /// refresh call itself) triggers one shared refresh; on success the
  /// original request is retried once with the new token, on failure the
  /// session is dropped and the error propagates as-is.
  Future<void> _onError(DioException err, ErrorInterceptorHandler handler) async {
    final path = err.requestOptions.path;
    final isAuthRoute = path.contains('/auth/login') || path.contains('/auth/register') || path.contains('/auth/refresh');
    if (err.response?.statusCode != 401 || isAuthRoute || _refreshToken == null) {
      return handler.next(err);
    }

    final refreshed = await (_refreshing ??= _doRefresh());
    _refreshing = null;
    if (!refreshed) {
      clearTokens();
      onSessionExpired?.call();
      return handler.next(err);
    }

    try {
      final retryOptions = err.requestOptions;
      retryOptions.headers['Authorization'] = 'Bearer $_accessToken';
      final response = await _dio.fetch(retryOptions);
      return handler.resolve(response);
    } on DioException catch (retryErr) {
      return handler.next(retryErr);
    }
  }

  Future<bool> _doRefresh() async {
    try {
      final r = await _dio.post('/api/v1/auth/refresh', data: {'refresh_token': _refreshToken});
      final body = r.data;
      if (body is! Map || body['access_token'] is! String) return false;
      _accessToken = body['access_token'] as String;
      return true;
    } catch (_) {
      return false;
    }
  }
}

typedef VoidCallback = void Function();
