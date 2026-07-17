import 'package:flutter/material.dart';

import '../theme/ac_colors.dart';
import '../theme/chamfer_border.dart';

/// Chamfer-cut panel — the app's standard card shell (ported concept from
/// SafeKitchen's TechBracketBox, but consistently chamfered per the brief
/// rather than that app's later drift to plain rounded corners).
/// [accent] set when the panel is highlighted (alert/selected) — becomes the
/// border color; [glow]=true adds a soft shadow in that color.
class OutlinePanel extends StatelessWidget {
  const OutlinePanel({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(16),
    this.accent,
    this.glow = false,
    this.cut = 12,
  });

  final Widget child;
  final EdgeInsetsGeometry padding;
  final Color? accent;
  final bool glow;
  final double cut;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final border = accent ?? ac.carbonLine;
    return Container(
      padding: padding,
      decoration: ShapeDecoration(
        color: ac.carbonPanel,
        shape: ChamferBorder(cut: cut, side: BorderSide(color: border, width: 2)),
        shadows: (glow && accent != null)
            ? [BoxShadow(color: accent!.withValues(alpha: 0.25), blurRadius: 16, spreadRadius: -4)]
            : null,
      ),
      child: child,
    );
  }
}
