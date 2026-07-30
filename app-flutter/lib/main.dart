import 'package:flutter/material.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'app/auth_gate.dart';
import 'services/api_client.dart';
import 'services/notification_store.dart';
import 'state/theme_controller.dart';
import 'theme/ac_theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final prefs = await SharedPreferences.getInstance();

  // Read the version from the INSTALLED package, not from a constant compiled
  // in by hand: pubspec is the single source of truth for versionCode/Name
  // (android/app/build.gradle.kts derives both from it), and anything we typed
  // separately could drift from the APK the user is actually running — which
  // would make the update check compare the wrong number.
  //
  // Resolved once, here, before runApp: ApiClient builds its Dio synchronously
  // and cannot await this.
  final info = await PackageInfo.fromPlatform();
  ApiClient.appVersion = '${info.version}+${info.buildNumber}';

  runApp(BreezeLinkApp(prefs: prefs));
}

class BreezeLinkApp extends StatelessWidget {
  const BreezeLinkApp({super.key, required this.prefs});

  final SharedPreferences prefs;

  @override
  Widget build(BuildContext context) {
    // NotificationStore đặt Ở ĐÂY, trên AuthGate: AuthGate hủy AppState mỗi lần
    // đăng xuất, còn lịch sử thông báo phải sống qua đó.
    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => ThemeController(prefs)),
        ChangeNotifierProvider(create: (_) => NotificationStore(prefs)),
      ],
      child: Consumer<ThemeController>(
        builder: (_, theme, _) => MaterialApp(
          title: 'BreezeLink',
          debugShowCheckedModeBanner: false,
          theme: AcTheme.light,
          darkTheme: AcTheme.dark,
          themeMode: theme.mode,
          home: AuthGate(prefs: prefs),
        ),
      ),
    );
  }
}
