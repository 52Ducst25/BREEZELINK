import 'package:flutter_secure_storage/flutter_secure_storage.dart';

/// "Remember login" storage — ported from SafeKitchen. Uses
/// flutter_secure_storage (Keystore/Keychain-backed) so the password is never
/// written as SharedPreferences plaintext. Only persists when the user opts
/// in; unchecking or logging out with "forget me" wipes it.
class CredentialStore {
  CredentialStore._();

  static const _storage = FlutterSecureStorage();

  static const _kRemember = 'remember_login';
  static const _kEmail = 'saved_email';
  static const _kPassword = 'saved_password';

  static Future<void> save(String email, String password) async {
    await _storage.write(key: _kRemember, value: '1');
    await _storage.write(key: _kEmail, value: email);
    await _storage.write(key: _kPassword, value: password);
  }

  static Future<void> clear() async {
    await _storage.delete(key: _kRemember);
    await _storage.delete(key: _kEmail);
    await _storage.delete(key: _kPassword);
  }

  static Future<SavedLogin> load() async {
    final remember = (await _storage.read(key: _kRemember)) == '1';
    return SavedLogin(
      remember: remember,
      email: (await _storage.read(key: _kEmail)) ?? '',
      password: (await _storage.read(key: _kPassword)) ?? '',
    );
  }
}

class SavedLogin {
  const SavedLogin({required this.remember, required this.email, required this.password});

  final bool remember;
  final String email;
  final String password;
}
