import 'package:dio/dio.dart';

/// A user-facing error — always a Vietnamese string, never a raw exception
/// message, so screens can show `e.message` directly.
class ApiException implements Exception {
  const ApiException(this.message);
  final String message;

  @override
  String toString() => message;
}

/// Translate a [DioException] (or the response-shape guard below) into a
/// Vietnamese message. Centralized so every API wrapper reports errors the
/// same way instead of re-deriving this switch per call site.
String translateDioError(DioException e, {required String baseUrl}) {
  final status = e.response?.statusCode;
  if (status == 401) return 'Phiên đăng nhập đã hết hạn. Vui lòng đăng nhập lại.';
  if (status == 403) return 'Bạn không có quyền thực hiện thao tác này.';
  if (status == 404) return 'Không tìm thấy dữ liệu yêu cầu.';
  if (status == 422) return _detailFrom(e) ?? 'Dữ liệu không hợp lệ.';
  if (status != null && status >= 500) return 'Máy chủ gặp sự cố. Vui lòng thử lại sau.';
  final fromBody = _detailFrom(e);
  if (fromBody != null) return fromBody;
  if (e.type == DioExceptionType.connectionError || e.type == DioExceptionType.connectionTimeout) {
    return 'Không kết nối được máy chủ ($baseUrl).';
  }
  if (e.type == DioExceptionType.receiveTimeout || e.type == DioExceptionType.sendTimeout) {
    return 'Máy chủ phản hồi quá chậm. Vui lòng thử lại.';
  }
  return 'Lỗi không xác định: ${e.message}';
}

/// FastAPI error bodies are `{"detail": "..."}` (or a validation-error list).
/// Pull a readable string out when present.
String? _detailFrom(DioException e) {
  final data = e.response?.data;
  if (data is Map && data['detail'] != null) {
    final d = data['detail'];
    if (d is String) return d;
    if (d is List && d.isNotEmpty) {
      final first = d.first;
      if (first is Map && first['msg'] != null) return first['msg'].toString();
    }
  }
  return null;
}

/// Guard against a tunnel/proxy returning HTML (or any non-JSON body) with a
/// 200 status — cast blind (`as Map`) and that becomes an uncaught TypeError
/// deep inside a model constructor instead of a clean user-facing message.
Map<String, dynamic> asMapOrThrow(dynamic data) {
  if (data is! Map) throw const ApiException('Máy chủ trả về dữ liệu không hợp lệ.');
  return data.cast<String, dynamic>();
}

List<dynamic> asListOrThrow(dynamic data) {
  if (data is! List) throw const ApiException('Máy chủ trả về dữ liệu không hợp lệ.');
  return data;
}
