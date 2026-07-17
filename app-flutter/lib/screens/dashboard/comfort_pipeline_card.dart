import 'package:flutter/material.dart';

import '../../models/comfort_preview.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/empty_value.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/section_label.dart';

/// Renders every intermediate of the comfort pipeline — T_rm -> t_neutral ->
/// humid_penalty -> t_target — so the final decision is auditable, not a
/// magic number (brief requirement). Each row falls back to an em-dash when
/// the server hasn't computed that stage yet ([ComfortPreview] fields are
/// nullable end-to-end); the [reason] string is always shown so an absent
/// decision is self-explanatory.
class ComfortPipelineCard extends StatelessWidget {
  const ComfortPipelineCard({super.key, required this.comfort});

  final ComfortPreview comfort;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return OutlinePanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SectionLabel('Chuỗi tính toán quyết định'),
          const SizedBox(height: 12),
          _row(context, 'T_rm (nhiệt độ TB ngoài trời)', comfort.tRm, '°C'),
          _arrow(ac),
          _row(context, 't_neutral (điểm trung tính ASHRAE)', comfort.tNeutral, '°C'),
          _arrow(ac),
          _row(context, 'humid_penalty (bù độ ẩm)', comfort.humidPenalty, '°C'),
          _arrow(ac),
          _row(context, 't_target (mục tiêu liên tục)', comfort.tTarget, '°C'),
          _arrow(ac),
          _row(context, 't_set (mã lệnh IR gần nhất)', comfort.tSet?.toDouble(), '°C'),
          const SizedBox(height: 10),
          Divider(color: ac.carbonLine),
          const SizedBox(height: 10),
          if (comfort.stale) ...[
            Row(
              children: [
                Icon(Icons.warning_amber_rounded, size: 15, color: ac.warning),
                const SizedBox(width: 6),
                Text('Dữ liệu ngoài trời có thể đã cũ (stale)', style: AcText.body(size: 11.5, color: ac.warning)),
              ],
            ),
            const SizedBox(height: 8),
          ],
          Text(comfort.reason, style: AcText.body(size: 12, color: ac.whiteDim)),
        ],
      ),
    );
  }

  Widget _row(BuildContext context, String label, double? value, String unit) {
    final ac = context.ac;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        children: [
          Expanded(child: Text(label, style: AcText.body(size: 12, color: ac.whiteDim))),
          EmptyValue(
            value?.toStringAsFixed(1),
            suffix: value == null ? '' : ' $unit',
            style: AcText.mono(size: 13, color: ac.white),
          ),
        ],
      ),
    );
  }

  Widget _arrow(AcPalette ac) => Padding(
        padding: const EdgeInsets.only(left: 2),
        child: Icon(Icons.arrow_downward, size: 12, color: ac.carbonLineBright),
      );
}
