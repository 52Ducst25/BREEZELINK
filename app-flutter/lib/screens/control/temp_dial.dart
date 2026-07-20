import 'package:flutter/material.dart';

import '../../models/ac_mode.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_shapes.dart';
import '../../theme/ac_text.dart';

/// The BenKon-style BIG CIRCULAR DIAL — a large soft-shadowed circle with the
/// current mode icon at the top, a very large temperature number in the centre
/// (JetBrains Mono), accent "−" / "+" round buttons flanking it, and a "°C"
/// pill underneath.
///
/// Purely presentational: it renders [mode]/[setpoint]/[on] and calls
/// [onDecrement]/[onIncrement] (null = disabled, dimmed). The setpoint is only
/// meaningful in COOL; DRY/FAN show the mode name and OFF shows a blank dash, in
/// which cases the caller passes null steppers.
class TempDial extends StatelessWidget {
  const TempDial({
    super.key,
    required this.mode,
    required this.setpoint,
    required this.on,
    required this.onDecrement,
    required this.onIncrement,
  });

  final AcMode mode;
  final int setpoint;
  final bool on;
  final VoidCallback? onDecrement;
  final VoidCallback? onIncrement;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final ring = on ? ac.ice : ac.carbonLine;

    return Center(
      child: Container(
        width: 260,
        height: 260,
        decoration: BoxDecoration(
          shape: BoxShape.circle,
          color: ac.carbonPanel,
          border: Border.all(color: ring, width: 6),
          boxShadow: [
            BoxShadow(color: Colors.black.withValues(alpha: 0.3), blurRadius: 24, offset: const Offset(0, 10)),
            if (on) BoxShadow(color: ac.iceGlow, blurRadius: 30, spreadRadius: -6),
          ],
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(mode.icon, size: 30, color: on ? ac.iceText : ac.whiteDim),
            const SizedBox(height: 8),
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                _StepButton(icon: Icons.remove, onTap: onDecrement),
                const SizedBox(width: 6),
                SizedBox(width: 120, child: Center(child: _centerValue(ac))),
                const SizedBox(width: 6),
                _StepButton(icon: Icons.add, onTap: onIncrement),
              ],
            ),
            const SizedBox(height: 10),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 4),
              decoration: BoxDecoration(color: ac.iceDim, borderRadius: pillRadius),
              child: Text('°C', style: AcText.mono(size: 13, color: ac.iceText)),
            ),
          ],
        ),
      ),
    );
  }

  Widget _centerValue(AcPalette ac) {
    if (!on) {
      return Text('TẮT', style: AcText.hero(size: 34, color: ac.whiteDim));
    }
    if (mode == AcMode.cool) {
      return Text('$setpoint', style: AcText.hero(size: 72, color: ac.white));
    }
    // DRY / FAN ignore the numeric setpoint — show the mode name instead.
    return Text(
      mode.label,
      textAlign: TextAlign.center,
      style: AcText.hero(size: 22, color: ac.white),
    );
  }
}

/// A round accent step button ("−" / "+"). Dimmed and inert when [onTap] null.
class _StepButton extends StatelessWidget {
  const _StepButton({required this.icon, required this.onTap});
  final IconData icon;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final enabled = onTap != null;
    return Material(
      color: Colors.transparent,
      shape: const CircleBorder(),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        customBorder: const CircleBorder(),
        child: Container(
          width: 42,
          height: 42,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: enabled ? ac.iceDim : ac.carbonUp,
          ),
          child: Icon(icon, size: 22, color: enabled ? ac.ice : ac.whiteDim),
        ),
      ),
    );
  }
}
