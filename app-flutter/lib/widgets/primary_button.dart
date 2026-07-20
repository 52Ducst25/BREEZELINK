import 'package:flutter/material.dart';

import '../theme/ac_colors.dart';
import '../theme/ac_shapes.dart';
import '../theme/ac_text.dart';

/// Standard action button (rounded). [primary]=true -> solid ice fill; else a
/// "soft" ghost variant (dim fill, ice text) for secondary actions.
class PrimaryButton extends StatelessWidget {
  const PrimaryButton({super.key, required this.label, required this.onPressed, this.primary = true, this.icon});

  final String label;
  final VoidCallback? onPressed;
  final bool primary;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final fg = primary ? Colors.white : ac.ice;
    return FilledButton(
      onPressed: onPressed,
      style: FilledButton.styleFrom(
        backgroundColor: primary ? ac.ice : ac.iceDim,
        foregroundColor: fg,
        elevation: 0,
        shape: acInnerShape(),
        padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 12),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (icon != null) ...[Icon(icon, size: 16, color: fg), const SizedBox(width: 8)],
          Text(label, style: AcText.label(size: 12, color: fg)),
        ],
      ),
    );
  }
}
