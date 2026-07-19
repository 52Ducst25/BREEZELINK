import 'package:flutter/material.dart';

import '../../models/live_reading.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../theme/thermal_scale.dart';
import '../../widgets/empty_value.dart';
import '../../widgets/outline_panel.dart';

/// Indoor/outdoor sensor tile. Color is driven by [ThermalState] (value
/// relative to the comfort neutral point) — indoor and outdoor use the exact
/// same color function, so color never encodes "which sensor" (brief rule).
/// Renders em-dashes when [reading] is null (no telemetry yet), never 0.0.
class ReadingTile extends StatelessWidget {
  const ReadingTile({
    super.key,
    required this.label,
    required this.reading,
    this.neutral,
  });

  final String label;
  final LiveReading? reading;

  /// Comfort pipeline's `t_neutral` for this cycle, when known — used to
  /// classify the thermal color band. Falls back to a generic band inside
  /// [classifyThermal] when null (e.g. before the first outdoor reading).
  final double? neutral;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final r = reading;
    final state = r == null
        ? null
        : classifyThermal(r.temp, neutral: neutral ?? 24);
    final color = state == null
        ? ac.whiteDim
        : state.colorFor(
            cold: ac.thermalCold,
            neutral: ac.thermalNeutral,
            warm: ac.thermalWarm,
            hot: ac.thermalHot,
          );

    return OutlinePanel(
      accent: state == null ? null : color,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label.toUpperCase(), style: AcText.label(color: ac.whiteDim)),
          const SizedBox(height: 8),
          Row(
            crossAxisAlignment: CrossAxisAlignment.baseline,
            textBaseline: TextBaseline.alphabetic,
            children: [
              EmptyValue(
                r?.temp.toStringAsFixed(1),
                style: AcText.hero(size: 32, color: color),
              ),
              Text('°C', style: AcText.label(size: 12, color: ac.whiteDim)),
            ],
          ),
          const SizedBox(height: 4),
          Row(
            children: [
              Icon(Icons.water_drop_outlined, size: 13, color: ac.whiteDim),
              const SizedBox(width: 4),
              EmptyValue(
                r?.humidity.toStringAsFixed(0),
                suffix: '%',
                style: AcText.mono(size: 12, color: ac.whiteDim),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
