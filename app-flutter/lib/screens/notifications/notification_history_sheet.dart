import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../models/app_notification.dart';
import '../../services/notification_store.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';

/// Bảng lịch sử thông báo, mở từ nút chuông trên thanh tiêu đề.
///
/// Là BOTTOM SHEET chứ không phải route đầy màn hình: đây là thứ người dùng xem
/// vài giây rồi đóng, không phải nơi để đi sâu. Sheet giữ được ngữ cảnh phía
/// sau nên không có cảm giác "bị đưa đi đâu mất".
Future<void> showNotificationHistory(BuildContext context) {
  final store = context.read<NotificationStore>();
  // Mở bảng RA là coi như đã trả lời câu "có gì mới không" — tắt dấu chấm ngay,
  // không đợi người dùng bấm vào từng dòng.
  store.markAllRead();

  return showModalBottomSheet<void>(
    context: context,
    isScrollControlled: true,
    backgroundColor: Colors.transparent,
    builder: (_) => ChangeNotifierProvider<NotificationStore>.value(
      value: store,
      child: const _HistorySheet(),
    ),
  );
}

class _HistorySheet extends StatelessWidget {
  const _HistorySheet();

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final store = context.watch<NotificationStore>();
    final items = store.items;

    return Container(
      // Trần 78% chiều cao: đủ để đọc, mà vẫn để lộ phần dưới màn hình phía sau
      // nên người dùng thấy ngay đây là lớp chồng tạm, kéo xuống là đóng.
      constraints: BoxConstraints(maxHeight: MediaQuery.of(context).size.height * 0.78),
      decoration: BoxDecoration(
        color: ac.carbonUp,
        borderRadius: const BorderRadius.vertical(top: Radius.circular(18)),
        border: Border(top: BorderSide(color: ac.carbonLine)),
      ),
      child: SafeArea(
        top: false,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            _header(context, ac, items.isNotEmpty),
            Divider(height: 1, color: ac.carbonLine),
            if (items.isEmpty)
              _empty(ac)
            else
              Flexible(
                child: ListView.separated(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  shrinkWrap: true,
                  itemCount: items.length,
                  separatorBuilder: (_, _) => Divider(height: 1, color: ac.carbonLine),
                  itemBuilder: (_, i) => _Row(item: items[i]),
                ),
              ),
          ],
        ),
      ),
    );
  }

  Widget _header(BuildContext context, AcPalette ac, bool hasItems) => Padding(
        padding: const EdgeInsets.only(left: 18, right: 8, top: 14, bottom: 12),
        child: Row(
          children: [
            Icon(Icons.notifications_outlined, size: 18, color: ac.ice),
            const SizedBox(width: 10),
            Expanded(child: Text('Thông báo', style: AcText.heading(size: 16, color: ac.white))),
            if (hasItems)
              TextButton(
                onPressed: () => context.read<NotificationStore>().clear(),
                child: Text('Xoá hết', style: AcText.body(size: 12.5, color: ac.whiteDim)),
              ),
            IconButton(
              onPressed: () => Navigator.of(context).pop(),
              icon: const Icon(Icons.close),
              iconSize: 20,
              color: ac.whiteDim,
              tooltip: 'Đóng',
            ),
          ],
        ),
      );

  Widget _empty(AcPalette ac) => Padding(
        padding: const EdgeInsets.fromLTRB(18, 28, 18, 34),
        child: Column(
          children: [
            Icon(Icons.notifications_none, size: 34, color: ac.iceGhost),
            const SizedBox(height: 12),
            Text('Chưa có thông báo nào', style: AcText.body(size: 13, color: ac.whiteDim)),
            const SizedBox(height: 4),
            Text(
              'Khi có bản cập nhật mới, thông báo sẽ hiện ở đây.',
              textAlign: TextAlign.center,
              style: AcText.body(size: 11.5, color: ac.iceDim),
            ),
          ],
        ),
      );
}

/// Một dòng thông báo, có nút × riêng để xoá đúng dòng đó.
class _Row extends StatelessWidget {
  const _Row({required this.item});

  final AppNotification item;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Padding(
      padding: const EdgeInsets.fromLTRB(18, 12, 6, 12),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Padding(
            padding: const EdgeInsets.only(top: 2),
            child: Icon(Icons.system_update, size: 17, color: ac.ice),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(item.title, style: AcText.heading(size: 13.5, color: ac.white)),
                if (item.body.isNotEmpty) ...[
                  const SizedBox(height: 3),
                  Text(item.body, style: AcText.body(size: 12, color: ac.whiteDim)),
                ],
                const SizedBox(height: 5),
                Text(_ago(item.ts), style: AcText.mono(size: 10.5, color: ac.iceDim)),
              ],
            ),
          ),
          IconButton(
            onPressed: () => context.read<NotificationStore>().remove(item.id),
            icon: const Icon(Icons.close),
            iconSize: 16,
            color: ac.iceDim,
            visualDensity: VisualDensity.compact,
            tooltip: 'Xoá thông báo này',
          ),
        ],
      ),
    );
  }

  /// "5 phút trước". Dùng thời gian TƯƠNG ĐỐI vì thông báo chỉ có ý nghĩa so với
  /// hiện tại — "14:32" bắt người đọc tự trừ nhẩm.
  static String _ago(DateTime ts) {
    final d = DateTime.now().difference(ts);
    if (d.inMinutes < 1) return 'Vừa xong';
    if (d.inMinutes < 60) return '${d.inMinutes} phút trước';
    if (d.inHours < 24) return '${d.inHours} giờ trước';
    if (d.inDays < 30) return '${d.inDays} ngày trước';
    return '${ts.day}/${ts.month}/${ts.year}';
  }
}
