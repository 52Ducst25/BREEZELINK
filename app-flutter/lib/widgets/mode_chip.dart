import 'package:flutter/material.dart';

import '../models/ac_mode.dart';
import '../theme/ac_colors.dart';
import '../theme/ac_shapes.dart';
import '../theme/ac_text.dart';

/// Small mode indicator (COOL/DRY/FAN/OFF) — icon + Vietnamese label. [mode]
/// null renders a neutral "chưa có quyết định" chip (never guesses a mode).
/// Rounded (stadium) pill to match the BenKon soft-card language.
class ModeChip extends StatelessWidget {
  const ModeChip({super.key, required this.mode, this.dense = false});

  final AcMode? mode;
  final bool dense;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final m = mode;
    final color = m == null ? ac.whiteDim : ac.ice;
    return Container(
      padding: EdgeInsets.symmetric(horizontal: dense ? 9 : 12, vertical: dense ? 5 : 7),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.14),
        borderRadius: pillRadius,
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(m?.icon ?? Icons.help_outline, size: dense ? 14 : 16, color: color),
          const SizedBox(width: 6),
          Text(m?.label ?? 'CHƯA CÓ', style: AcText.label(size: dense ? 10 : 11, color: color)),
        ],
      ),
    );
  }
}
