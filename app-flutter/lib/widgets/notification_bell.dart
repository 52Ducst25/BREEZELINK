import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../screens/notifications/notification_history_sheet.dart';
import '../services/notification_store.dart';
import '../theme/ac_colors.dart';

/// Nút chuông trên thanh tiêu đề, kèm dấu chấm khi có thông báo chưa đọc.
///
/// Chỉ hiện DẤU CHẤM, không hiện con số. Nguồn thông báo duy nhất hiện nay là
/// bản cập nhật mới, và số lượng ở đây luôn là 1 — một con số "1" trong vòng
/// tròn đỏ chỉ tốn chỗ mà không nói thêm điều gì so với một dấu chấm.
class NotificationBell extends StatelessWidget {
  const NotificationBell({super.key});

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final hasUnread = context.watch<NotificationStore>().hasUnread;

    return IconButton(
      onPressed: () => showNotificationHistory(context),
      tooltip: hasUnread ? 'Có thông báo mới' : 'Thông báo',
      icon: Stack(
        // Cho dấu chấm tràn ra ngoài khung icon — nếu clip thì nó bị cắt mất
        // một góc và nhìn như lỗi vẽ.
        clipBehavior: Clip.none,
        children: [
          Icon(hasUnread ? Icons.notifications : Icons.notifications_outlined),
          if (hasUnread)
            Positioned(
              right: -1,
              top: -1,
              child: Container(
                width: 9,
                height: 9,
                decoration: BoxDecoration(
                  color: ac.error,
                  shape: BoxShape.circle,
                  // Viền cùng màu nền thanh tiêu đề: tách dấu chấm khỏi nét
                  // chuông phía dưới, nếu không hai thứ dính vào nhau thành một
                  // khối nhoè khi chuông đang ở trạng thái đặc.
                  border: Border.all(color: ac.carbon, width: 1.5),
                ),
              ),
            ),
        ],
      ),
    );
  }
}
