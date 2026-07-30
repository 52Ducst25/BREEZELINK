/// Một dòng trong lịch sử thông báo của app.
///
/// Lưu tại MÁY, không phải từ máy chủ: hệ chưa có API thông báo, nguồn duy nhất
/// hiện tại là lần kiểm cập nhật OTA phát hiện có bản mới. Cấu trúc để mở sẵn
/// cho nguồn khác (mất kết nối node, học mã thất bại…) qua trường [kind].
class AppNotification {
  const AppNotification({
    required this.id,
    required this.kind,
    required this.title,
    required this.body,
    required this.ts,
    required this.read,
  });

  /// Khoá CHỐNG TRÙNG. Với bản cập nhật là "update:21" — mỗi lần mở app đều
  /// kiểm OTA, không có khoá này thì cùng một bản sẽ đẻ ra một dòng mới mỗi
  /// lần mở, lịch sử biến thành rác trong vài ngày.
  final String id;

  /// Loại nguồn — hiện chỉ có "update", để dành cho về sau.
  final String kind;

  final String title;
  final String body;
  final DateTime ts;
  final bool read;

  AppNotification copyWith({bool? read}) => AppNotification(
        id: id,
        kind: kind,
        title: title,
        body: body,
        ts: ts,
        read: read ?? this.read,
      );

  Map<String, dynamic> toJson() => {
        'id': id,
        'kind': kind,
        'title': title,
        'body': body,
        'ts': ts.toIso8601String(),
        'read': read,
      };

  /// Trả null nếu dòng lưu bị hỏng thay vì ném.
  ///
  /// Dữ liệu này nằm trong SharedPreferences của máy khách và sống qua nhiều
  /// bản app; một lần đổi cấu trúc trong tương lai sẽ gặp lại các dòng cũ. Ném
  /// ở đây là app không mở được, mà chỉ vì một dòng thông báo cũ.
  static AppNotification? fromJsonOrNull(Map<String, dynamic> j) {
    final id = j['id'];
    final ts = DateTime.tryParse((j['ts'] ?? '') as String);
    if (id is! String || id.isEmpty || ts == null) return null;
    return AppNotification(
      id: id,
      kind: (j['kind'] ?? 'update') as String,
      title: (j['title'] ?? '') as String,
      body: (j['body'] ?? '') as String,
      ts: ts,
      read: (j['read'] ?? false) as bool,
    );
  }
}
