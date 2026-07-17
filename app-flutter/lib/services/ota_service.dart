import 'package:dio/dio.dart';
import 'package:package_info_plus/package_info_plus.dart';

/// What the server says the newest build is.
class AppUpdateInfo {
  const AppUpdateInfo({
    required this.latestVersionCode,
    required this.latestVersionName,
    required this.apkUrl,
    required this.changelog,
    required this.minSupportedVersionCode,
    required this.apkSize,
  });

  final int latestVersionCode;
  final String latestVersionName;
  final String apkUrl;
  final String changelog;
  final int minSupportedVersionCode;
  final int apkSize;

  /// Keys are camelCase and must match services/release_service.build_manifest
  /// on the server exactly — a typo here silently yields 0 and the app then
  /// believes it is always up to date.
  static AppUpdateInfo? fromJson(Map<String, dynamic> j) {
    final code = j['latestVersionCode'];
    if (code is! int || code <= 0) return null; // nothing published yet
    return AppUpdateInfo(
      latestVersionCode: code,
      latestVersionName: (j['latestVersionName'] ?? '') as String,
      apkUrl: (j['apkUrl'] ?? '') as String,
      changelog: (j['changelog'] ?? '') as String,
      minSupportedVersionCode: (j['minSupportedVersionCode'] ?? 1) as int,
      apkSize: (j['apkSize'] ?? 0) as int,
    );
  }
}

/// Result of comparing this build against the server's.
class UpdateCheck {
  const UpdateCheck({required this.info, required this.available, required this.forced});

  final AppUpdateInfo info;

  /// Server has a higher build number than this one.
  final bool available;

  /// This build is below the server's floor and must update to keep working.
  final bool forced;
}

/// Checks whether a newer build has been published.
///
/// Uses its OWN Dio, not [ApiClient]: the update check runs before login (and
/// must still work for a build too old to speak the current API), so it cannot
/// depend on tokens or on the authenticated client's refresh machinery. The
/// endpoint is deliberately public — see api/ota_routes on the server.
class OtaService {
  OtaService(this.baseUrl);

  final String baseUrl;

  Future<UpdateCheck?> check() async {
    final int current = await _currentBuildNumber();
    if (current <= 0) return null;

    try {
      final dio = Dio(BaseOptions(
        baseUrl: baseUrl,
        connectTimeout: const Duration(seconds: 8),
        receiveTimeout: const Duration(seconds: 8),
      ));
      final res = await dio.get<Map<String, dynamic>>('/app/update.json');
      final data = res.data;
      if (data == null) return null;
      final info = AppUpdateInfo.fromJson(data);
      if (info == null) return null;

      return UpdateCheck(
        info: info,
        // Strictly greater: equal means we ARE the current build.
        available: info.latestVersionCode > current,
        forced: current < info.minSupportedVersionCode,
      );
    } catch (_) {
      // Never surface a failed check as an error. It runs on launch, and a
      // flaky network must not greet the customer with a scary dialog about
      // something they did not ask for.
      return null;
    }
  }

  /// Absolute URL to hand the browser. The manifest carries a RELATIVE apkUrl
  /// so the same release works on any host the vendor points the app at.
  String downloadUrl(AppUpdateInfo info) => '$baseUrl${info.apkUrl}';

  static Future<int> _currentBuildNumber() async {
    try {
      final info = await PackageInfo.fromPlatform();
      return int.tryParse(info.buildNumber) ?? 0;
    } catch (_) {
      return 0;
    }
  }
}
