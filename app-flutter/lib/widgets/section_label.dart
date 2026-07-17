import 'package:flutter/material.dart';

import '../theme/ac_colors.dart';
import '../theme/ac_text.dart';

/// Section overline: ice accent tick + small caps label. Ported from
/// SafeKitchen's SectionLabel pattern.
class SectionLabel extends StatelessWidget {
  const SectionLabel(this.text, {super.key, this.color});

  final String text;
  final Color? color;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final color = this.color ?? ac.whiteDim;
    return Row(
      children: [
        Container(width: 3, height: 13, color: ac.ice), // square accent — no rounded corners
        const SizedBox(width: 8),
        Expanded(
          child: Text(
            text.toUpperCase(),
            style: AcText.label(size: 11, color: color),
            overflow: TextOverflow.ellipsis,
          ),
        ),
      ],
    );
  }
}
