import 'package:flutter/material.dart';

import '../../models/energy_overview.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/icon_badge.dart';
import '../../widgets/outline_panel.dart';

/// Dashboard POWER CONSUMPTION card. Big kWh number for today + a "x% so với
/// hôm qua" delta. Tapping opens the full energy screen.
///
/// [energy] null (endpoint unavailable) OR a null total renders "Chưa có dữ
/// liệu điện" — no fabricated 0 kWh / 0 %.
class PowerConsumptionCard extends StatelessWidget {
  const PowerConsumptionCard({super.key, required this.energy, required this.onTap});

  final EnergyOverview? energy;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final total = energy?.totalKwhToday;
    final pct = energy?.pctVsYesterday;

    return OutlinePanel(
      onTap: onTap,
      padding: const EdgeInsets.all(18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              AcIconBadge(icon: Icons.bolt_outlined, size: 40, iconSize: 20, color: ac.warning),
              const SizedBox(width: 12),
              Expanded(
                child: Text('Điện năng tiêu thụ', style: AcText.heading(size: 15, color: ac.white)),
              ),
              Icon(Icons.chevron_right, color: ac.whiteDim, size: 20),
            ],
          ),
          const SizedBox(height: 16),
          if (total == null)
            Text('Chưa có dữ liệu điện', style: AcText.body(size: 13, color: ac.whiteDim))
          else ...[
            Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                Text(total.toStringAsFixed(2), style: AcText.hero(size: 34, color: ac.white)),
                const SizedBox(width: 6),
                Text('kWh hôm nay', style: AcText.body(size: 13, color: ac.whiteDim)),
              ],
            ),
            const SizedBox(height: 8),
            _DeltaLine(pct: pct),
          ],
        ],
      ),
    );
  }
}

/// "x% so với hôm qua" with a direction arrow. When [pct] is null we still have
/// a total but no comparison baseline — say so honestly.
class _DeltaLine extends StatelessWidget {
  const _DeltaLine({required this.pct});
  final double? pct;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final p = pct;
    if (p == null) {
      return Text('Chưa có số liệu so sánh với hôm qua', style: AcText.body(size: 12, color: ac.whiteDim));
    }
    final up = p >= 0;
    // More power than yesterday reads as "worse" (warm), less as "better".
    final color = up ? ac.warning : ac.success;
    return Row(
      children: [
        Icon(up ? Icons.trending_up : Icons.trending_down, size: 16, color: color),
        const SizedBox(width: 6),
        Text('${up ? '+' : ''}${p.toStringAsFixed(0)}%', style: AcText.mono(size: 13, color: color)),
        const SizedBox(width: 6),
        Text('so với hôm qua', style: AcText.body(size: 12, color: ac.whiteDim)),
      ],
    );
  }
}
