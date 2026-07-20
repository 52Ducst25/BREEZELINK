import 'package:flutter/material.dart';

import '../theme/ac_colors.dart';
import '../theme/ac_shapes.dart';

/// A leading rounded-square icon tile — the per-item glyph BenKon puts in front
/// of every list row / card. Tinted [color] background at low alpha with the
/// icon in the full color.
class AcIconBadge extends StatelessWidget {
  const AcIconBadge({super.key, required this.icon, this.color, this.size = 44, this.iconSize = 22});

  final IconData icon;
  final Color? color;
  final double size;
  final double iconSize;

  @override
  Widget build(BuildContext context) {
    final c = color ?? context.ac.ice;
    return Container(
      width: size,
      height: size,
      decoration: BoxDecoration(
        color: c.withValues(alpha: 0.14),
        borderRadius: innerRadius,
      ),
      child: Icon(icon, size: iconSize, color: c),
    );
  }
}
