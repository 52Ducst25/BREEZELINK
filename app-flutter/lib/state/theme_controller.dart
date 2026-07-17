import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Light/dark/system theme choice, persisted via SharedPreferences — ported
/// from SafeKitchen's ThemeController pattern verbatim (domain-agnostic).
class ThemeController extends ChangeNotifier {
  ThemeController(this._prefs) : _mode = _decode(_prefs.getString(_key));

  static const _key = 'theme_mode';
  final SharedPreferences _prefs;
  ThemeMode _mode;

  ThemeMode get mode => _mode;

  Future<void> setMode(ThemeMode mode) async {
    if (mode == _mode) return;
    _mode = mode;
    notifyListeners();
    await _prefs.setString(_key, mode.name);
  }

  static ThemeMode _decode(String? v) {
    switch (v) {
      case 'light':
        return ThemeMode.light;
      case 'dark':
        return ThemeMode.dark;
      case 'system':
        return ThemeMode.system;
      default:
        return ThemeMode.dark; // cooling/AC identity reads best dark by default
    }
  }
}
