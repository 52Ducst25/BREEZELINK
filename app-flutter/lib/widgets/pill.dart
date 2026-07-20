import 'package:flutter/material.dart';

import '../theme/ac_colors.dart';
import '../theme/ac_shapes.dart';
import '../theme/ac_text.dart';

/// A small rounded (stadium) status pill — BenKon's signature chip for mode,
/// setpoint and online/offline state. [filled]=true tints the background in
/// [color] at low alpha; else it is outlined. An optional leading [icon] and a
/// pulsing [dot] (online indicator) are supported.
class AcPill extends StatelessWidget {
  const AcPill({
    super.key,
    required this.label,
    this.icon,
    this.color,
    this.filled = true,
    this.dense = false,
    this.leading,
  });

  final String label;
  final IconData? icon;
  final Color? color;
  final bool filled;
  final bool dense;

  /// Custom leading widget (e.g. a StatusDot) shown before the label.
  final Widget? leading;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final c = color ?? ac.ice;
    return Container(
      padding: EdgeInsets.symmetric(horizontal: dense ? 9 : 11, vertical: dense ? 5 : 6),
      decoration: BoxDecoration(
        color: filled ? c.withValues(alpha: 0.14) : Colors.transparent,
        borderRadius: pillRadius,
        border: Border.all(color: filled ? Colors.transparent : c, width: 1.4),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (leading != null) ...[leading!, const SizedBox(width: 6)],
          if (icon != null) ...[Icon(icon, size: dense ? 13 : 15, color: c), const SizedBox(width: 5)],
          Text(label, style: AcText.label(size: dense ? 10 : 11, color: c)),
        ],
      ),
    );
  }
}
