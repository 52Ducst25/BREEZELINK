import 'dart:convert';

import 'package:flutter_secure_storage/flutter_secure_storage.dart';

/// Saved-logins store — MULTIPLE accounts (email → password), each encrypted by
/// flutter_secure_storage (Keystore/Keychain), never SharedPreferences
/// plaintext. Powers the login screen's "pick a saved account → auto-fill
/// password" flow. Only accounts the user chose to remember are kept.
class CredentialStore {
  CredentialStore._();

  static const _storage = FlutterSecureStorage();
  static const _kAccounts = 'saved_accounts'; // JSON {email: password}
  static const _kLast = 'last_email'; // which one to pre-fill on launch

  static Future<Map<String, String>> _readAll() async {
    final raw = await _storage.read(key: _kAccounts);
    if (raw == null || raw.isEmpty) return {};
    try {
      final m = jsonDecode(raw) as Map<String, dynamic>;
      return m.map((k, v) => MapEntry(k, v as String));
    } catch (_) {
      return {}; // corrupt blob — treat as empty rather than crash the login screen
    }
  }

  /// Remember (or update) one account and mark it the most recent.
  static Future<void> saveAccount(String email, String password) async {
    final all = await _readAll();
    all[email] = password;
    await _storage.write(key: _kAccounts, value: jsonEncode(all));
    await _storage.write(key: _kLast, value: email);
  }

  /// Forget one account (leaving the others). Used when the user logs in with
  /// "remember" unchecked, or removes an entry from the picker.
  static Future<void> removeAccount(String email) async {
    final all = await _readAll();
    all.remove(email);
    await _storage.write(key: _kAccounts, value: jsonEncode(all));
    if ((await _storage.read(key: _kLast)) == email) {
      await _storage.delete(key: _kLast);
    }
  }

  /// Saved emails for the picker, most-recent first.
  static Future<List<String>> listEmails() async {
    final all = await _readAll();
    final last = await _storage.read(key: _kLast);
    final emails = all.keys.toList();
    if (last != null && emails.remove(last)) emails.insert(0, last);
    return emails;
  }

  static Future<String?> passwordFor(String email) async => (await _readAll())[email];

  /// The account to pre-fill on launch (the last one used), or empty.
  static Future<SavedLogin> load() async {
    final all = await _readAll();
    if (all.isEmpty) return const SavedLogin(remember: false, email: '', password: '');
    final last = await _storage.read(key: _kLast);
    final email = (last != null && all.containsKey(last)) ? last : all.keys.first;
    return SavedLogin(remember: true, email: email, password: all[email] ?? '');
  }

  /// Wipe ALL saved accounts.
  static Future<void> clear() async {
    await _storage.delete(key: _kAccounts);
    await _storage.delete(key: _kLast);
  }
}

class SavedLogin {
  const SavedLogin({required this.remember, required this.email, required this.password});

  final bool remember;
  final String email;
  final String password;
}
