import 'api_client.dart';
import 'api_exception.dart';
import '../models/user_profile.dart';

/// `/auth/*` calls. Login/register validate the token-pair shape before
/// touching [ApiClient] state — a tunnel returning HTML with a 200 must not
/// silently "log in" with a null token (brief rule: validate before casting).
class AuthApi {
  AuthApi(this._client);
  final ApiClient _client;

  /// Returns null on success, else a Vietnamese error to display.
  Future<String?> login(String email, String password) async {
    try {
      final body = await _client.postPublic('/api/v1/auth/login', data: {
        'email': email,
        'password': password,
      });
      final access = body['access_token'];
      final refresh = body['refresh_token'];
      if (access is! String || access.isEmpty || refresh is! String || refresh.isEmpty) {
        return 'Máy chủ trả về dữ liệu không hợp lệ.';
      }
      _client.setTokens(access: access, refresh: refresh);
      return null;
    } on ApiException catch (e) {
      return e.message;
    }
  }

  /// Redeem an activation code into an account.
  ///
  /// The vendor creates the household and hands over the code; the app never
  /// names an organization any more (the API rejects that shape outright — see
  /// schemas/auth.RegisterRequest).
  Future<String?> register({
    required String code,
    required String email,
    required String password,
    String? fullName,
    String? phone,
  }) async {
    try {
      final body = await _client.postPublic('/api/v1/auth/register', data: {
        'code': code,
        'email': email,
        'password': password,
        if (fullName != null && fullName.isNotEmpty) 'full_name': fullName,
        if (phone != null && phone.isNotEmpty) 'phone': phone,
      });
      final access = body['access_token'];
      final refresh = body['refresh_token'];
      if (access is! String || refresh is! String) {
        return 'Máy chủ trả về dữ liệu không hợp lệ.';
      }
      _client.setTokens(access: access, refresh: refresh);
      return null;
    } on ApiException catch (e) {
      return e.message;
    }
  }

  Future<UserProfile?> me() async {
    try {
      final body = await _client.get('/api/v1/auth/me');
      return UserProfile.fromJson(body);
    } on ApiException {
      return null;
    }
  }

  /// Edit the caller's own profile. Only the non-null fields are sent, so the
  /// account screen can save just the home address. Throws [ApiException] on
  /// failure so the caller can surface the reason.
  Future<UserProfile> updateProfile({
    String? fullName,
    String? phone,
    String? location,
    double? latitude,
    double? longitude,
  }) async {
    final body = await _client.put('/api/v1/auth/me', data: {
      'full_name': ?fullName,
      'phone': ?phone,
      'location': ?location,
      'latitude': ?latitude,
      'longitude': ?longitude,
    });
    return UserProfile.fromJson(body);
  }

  Future<String?> forgotPassword(String email) async {
    try {
      await _client.postPublic('/api/v1/auth/forgot-password', data: {'email': email});
      return null;
    } on ApiException catch (e) {
      return e.message;
    }
  }
}
