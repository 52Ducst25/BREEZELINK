import 'dart:math';

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

import '../../models/energy_series.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/outline_panel.dart';

/// Power (W) line chart for an energy window — adapted from the telemetry
/// chart. Renders "chưa có dữ liệu" when the series is empty; the Y axis
/// auto-scales to the real data range.
class WattChart extends StatelessWidget {
  const WattChart({super.key, required this.series});

  final EnergySeries series;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final points = series.wattSeries;

    if (points.length < 2) {
      return OutlinePanel(
        child: SizedBox(
          height: 200,
          child: Center(child: Text('Chưa có dữ liệu điện năng', style: AcText.body(color: ac.whiteDim))),
        ),
      );
    }

    final values = [for (final p in points) p.watt];
    var lo = values.reduce(min);
    var hi = values.reduce(max);
    if ((hi - lo).abs() < 1e-6) {
      lo -= 1;
      hi += 1;
    }
    lo = min(lo, 0); // watts are non-negative; anchor the floor at 0 when possible
    final pad = (hi - lo) * 0.15;
    final minY = lo;
    final maxY = hi + pad;
    final interval = max((maxY - minY) / 4, 0.0001);

    final spots = [for (var i = 0; i < values.length; i++) FlSpot(i.toDouble(), values[i])];

    return OutlinePanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('CÔNG SUẤT (W)', style: AcText.label(color: ac.whiteDim)),
          const SizedBox(height: 14),
          SizedBox(
            height: 220,
            child: LineChart(
              LineChartData(
                minY: minY,
                maxY: maxY,
                gridData: FlGridData(
                  show: true,
                  drawVerticalLine: false,
                  horizontalInterval: interval,
                  getDrawingHorizontalLine: (_) => FlLine(color: ac.carbonLine, strokeWidth: 1),
                ),
                titlesData: FlTitlesData(
                  leftTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      interval: interval,
                      reservedSize: 44,
                      getTitlesWidget: (v, m) => Text(v.toStringAsFixed(0), style: AcText.mono(size: 8, color: ac.whiteDim)),
                    ),
                  ),
                  rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                  topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                  bottomTitles: AxisTitles(
                    sideTitles: SideTitles(
                      showTitles: true,
                      reservedSize: 24,
                      interval: max((spots.length - 1) / 3, 1).floorToDouble(),
                      getTitlesWidget: (v, m) {
                        final i = v.round().clamp(0, points.length - 1);
                        return Text(DateFormat('HH:mm').format(points[i].ts), style: AcText.label(size: 8, color: ac.whiteDim));
                      },
                    ),
                  ),
                ),
                borderData: FlBorderData(show: false),
                lineTouchData: LineTouchData(
                  touchTooltipData: LineTouchTooltipData(
                    getTooltipColor: (_) => ac.carbonUp,
                    getTooltipItems: (touched) => [
                      for (final t in touched)
                        LineTooltipItem(
                          '${t.y.toStringAsFixed(0)} W\n${DateFormat('dd/MM HH:mm').format(points[t.spotIndex.clamp(0, points.length - 1)].ts)}',
                          AcText.mono(size: 11, color: ac.ice),
                        ),
                    ],
                  ),
                ),
                lineBarsData: [
                  LineChartBarData(
                    spots: spots,
                    isCurved: true,
                    color: ac.ice,
                    barWidth: 2,
                    dotData: const FlDotData(show: false),
                    belowBarData: BarAreaData(show: true, color: ac.ice.withValues(alpha: 0.1)),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
