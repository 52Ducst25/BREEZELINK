import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../models/app_notification.dart';

/// Lịch sử thông báo, lưu trong SharedPreferences.
///
/// Đặt TRÊN AuthGate (main.dart) chứ không nằm trong AppState: AppState bị hủy
/// mỗi lần đăng xuất, mà lịch sử thông báo phải sống qua đó — người dùng đăng
/// xuất rồi vào lại vẫn thấy thông báo cũ.
class NotificationStore extends ChangeNotifier {
  NotificationStore(this._prefs) {
    _load();
  }

  static const _key = 'notifications_v1';

  /// Giữ tối đa 50 dòng. Không có trần thì SharedPreferences phình vô hạn —
  /// nó đọc/ghi TOÀN BỘ tệp mỗi lần, nên một lịch sử dài làm chậm cả lúc mở app.
  static const _maxItems = 50;

  final SharedPreferences _prefs;
  List<AppNotification> _items = const [];

  /// Mới nhất lên đầu.
  List<AppNotification> get items => _items;

  int get unreadCount => _items.where((n) => !n.read).length;
  bool get hasUnread => unreadCount > 0;

  void _load() {
    final raw = _prefs.getStringList(_key) ?? const [];
    final parsed = <AppNotification>[];
    for (final s in raw) {
      try {
        final j = jsonDecode(s);
        if (j is Map<String, dynamic>) {
          final n = AppNotification.fromJsonOrNull(j);
          if (n != null) parsed.add(n);
        }
      } catch (_) {
        // Bỏ qua dòng hỏng — xem chú thích ở fromJsonOrNull.
      }
    }
    parsed.sort((a, b) => b.ts.compareTo(a.ts));
    _items = parsed;
  }

  Future<void> _persist() async {
    await _prefs.setStringList(
      _key,
      _items.map((n) => jsonEncode(n.toJson())).toList(),
    );
  }

  /// Thêm một thông báo. Bỏ qua nếu [AppNotification.id] đã có.
  ///
  /// Trả về true khi thực sự thêm mới — chỗ gọi dùng nó để biết có nên hiện
  /// hộp thoại hay không.
  Future<bool> add(AppNotification n) async {
    if (_items.any((e) => e.id == n.id)) return false;
    _items = [n, ..._items].take(_maxItems).toList();
    await _persist();
    notifyListeners();
    return true;
  }

  /// Đánh dấu đã đọc HẾT. Gọi khi người dùng mở bảng lịch sử — dấu chấm đỏ trả
  /// lời câu "có gì mới không", nên mở bảng ra là đã trả lời xong.
  Future<void> markAllRead() async {
    if (!hasUnread) return;
    _items = _items.map((n) => n.copyWith(read: true)).toList();
    await _persist();
    notifyListeners();
  }

  Future<void> remove(String id) async {
    final next = _items.where((n) => n.id != id).toList();
    if (next.length == _items.length) return;
    _items = next;
    await _persist();
    notifyListeners();
  }

  Future<void> clear() async {
    if (_items.isEmpty) return;
    _items = const [];
    await _persist();
    notifyListeners();
  }
}
