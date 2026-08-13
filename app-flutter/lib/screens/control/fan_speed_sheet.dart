import 'package:flutter/material.dart';

import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';

/// Sáu mức quạt, kèm nhãn NGẮN cho bảng chọn.
///
/// Nhãn dài ("Quạt 20%") nằm ở `kAcActions` để màn Học lệnh đọc; ở đây chỉ cần
/// "20%" cho đỡ chật. Thứ tự khớp thứ tự hiện trên bảng.
const kFanChoices = <(String, String)>[
  ('FAN_20', '20%'),
  ('FAN_40', '40%'),
  ('FAN_60', '60%'),
  ('FAN_80', '80%'),
  ('FAN_100', '100%'),
  ('FAN_AUTO', 'Tự động'),
];

/// Mở bảng chọn tốc độ quạt. Trả về mã nút được chọn, hoặc null nếu đóng bảng.
Future<String?> showFanSpeedSheet(BuildContext context, String? current) {
  return showModalBottomSheet<String>(
    context: context,
    backgroundColor: Colors.transparent,
    builder: (_) => _FanSheet(current: current),
  );
}

class _FanSheet extends StatelessWidget {
  const _FanSheet({required this.current});

  /// Mức app này vừa gửi — chỉ để tô sáng ô tương ứng. Không phải trạng thái
  /// thật của máy; xem `_fanWire` trong override_panel.dart.
  final String? current;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Container(
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
            Padding(
              padding: const EdgeInsets.only(left: 18, right: 8, top: 14, bottom: 10),
              child: Row(
                children: [
                  Icon(Icons.air, size: 18, color: ac.ice),
                  const SizedBox(width: 10),
                  Expanded(child: Text('Tốc độ quạt', style: AcText.heading(size: 16, color: ac.white))),
                  IconButton(
                    onPressed: () => Navigator.of(context).pop(),
                    icon: const Icon(Icons.close),
                    iconSize: 20,
                    color: ac.whiteDim,
                    tooltip: 'Đóng',
                  ),
                ],
              ),
            ),
            Divider(height: 1, color: ac.carbonLine),
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 14, 16, 6),
              child: GridView.count(
                crossAxisCount: 3,
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                mainAxisSpacing: 10,
                crossAxisSpacing: 10,
                childAspectRatio: 1.5,
                children: [
                  for (final (wire, label) in kFanChoices)
                    _FanTile(
                      label: label,
                      selected: wire == current,
                      onTap: () => Navigator.of(context).pop(wire),
                    ),
                ],
              ),
            ),
            //  Câu này phải có. Mỗi mức là một mã hồng ngoại HỌC RIÊNG, mà remote
            //  thật thường chỉ có 3 nấc (Low/Med/High) chứ hiếm khi đủ 5. Không
            //  nói ra thì người dùng bấm 40%, nhận lỗi "chưa học", và tưởng app hỏng.
            Padding(
              padding: const EdgeInsets.fromLTRB(18, 4, 18, 16),
              child: Text(
                'Mỗi mức là một mã học riêng từ remote. Mức nào chưa học sẽ báo khi bấm — '
                'vào tab Học lệnh để học thêm.',
                style: AcText.body(size: 11, color: ac.iceDim),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _FanTile extends StatelessWidget {
  const _FanTile({required this.label, required this.selected, required this.onTap});

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final border = selected ? ac.ice : ac.carbonLine;
    return Material(
      color: selected ? ac.ice.withValues(alpha: 0.14) : ac.carbonPanel,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: BorderSide(color: border, width: selected ? 1.5 : 1),
      ),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        child: Center(
          child: Text(
            label,
            style: AcText.heading(size: 15, color: selected ? ac.ice : ac.white),
          ),
        ),
      ),
    );
  }
}
