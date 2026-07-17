import 'package:flutter/material.dart';

import '../theme/ac_colors.dart';
import '../theme/ac_text.dart';

/// Shown when the realtime feed is disconnected (neither WS nor the last
/// poll succeeded) — tells the user displayed numbers may be stale, instead
/// of silently showing old data as if it were current.
class OfflineBanner extends StatelessWidget {
  const OfflineBanner({super.key});

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      decoration: BoxDecoration(
        color: ac.warning.withValues(alpha: 0.12),
        border: Border.all(color: ac.warning.withValues(alpha: 0.5), width: 2),
      ),
      child: Row(
        children: [
          Icon(Icons.cloud_off_outlined, color: ac.warning, size: 18),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              'Mất kết nối máy chủ — số liệu có thể đã cũ.',
              style: AcText.body(size: 12.5, color: ac.warning),
            ),
          ),
        ],
      ),
    );
  }
}
