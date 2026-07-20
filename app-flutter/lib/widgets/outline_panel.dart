import 'package:flutter/material.dart';

import '../theme/ac_colors.dart';
import '../theme/ac_shapes.dart';

/// The app's standard card shell — now ROUNDED (BenKon-style soft card) rather
/// than the former chamfer cut. Generous padding, a hairline border and a soft
/// drop shadow give the lifted, whitespace-forward look while keeping the dark
/// carbon surface.
///
/// [accent] set when the panel is highlighted (alert/selected) — becomes the
/// border color; [glow]=true adds a soft shadow in that color. [onTap] makes
/// the whole card a ripple target (power / device cards) with a rounded clip.
class OutlinePanel extends StatelessWidget {
  const OutlinePanel({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(16),
    this.accent,
    this.glow = false,
    this.radius = AcRadii.card,
    this.onTap,
  });

  final Widget child;
  final EdgeInsetsGeometry padding;
  final Color? accent;
  final bool glow;
  final double radius;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final border = accent ?? ac.carbonLine;
    final shape = RoundedRectangleBorder(
      borderRadius: BorderRadius.circular(radius),
      side: BorderSide(color: border, width: accent != null ? 1.5 : 1),
    );

    // Soft depth: an accent glow when highlighted, else a subtle neutral shadow
    // so cards read as lifted panels on the dark background.
    final shadows = (glow && accent != null)
        ? [BoxShadow(color: accent!.withValues(alpha: 0.25), blurRadius: 18, spreadRadius: -4)]
        : [BoxShadow(color: Colors.black.withValues(alpha: 0.22), blurRadius: 14, offset: const Offset(0, 6))];

    final content = Container(
      padding: padding,
      decoration: ShapeDecoration(color: ac.carbonPanel, shape: shape, shadows: shadows),
      child: child,
    );

    if (onTap == null) return content;
    // Shadow on an outer (unclipped) layer; the Material supplies the fill, the
    // rounded clip and the ink ripple.
    return DecoratedBox(
      decoration: ShapeDecoration(shape: shape, shadows: shadows),
      child: Material(
        color: ac.carbonPanel,
        shape: shape,
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: onTap,
          child: Padding(padding: padding, child: child),
        ),
      ),
    );
  }
}
