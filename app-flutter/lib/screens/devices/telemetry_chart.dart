import 'dart:math';

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

import '../../models/telemetry_sample.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/outline_panel.dart';

/// Temperature history line chart for one device — adapted from SafeKitchen's
/// connected-scatterplot pattern (fl_chart), Y axis auto-scales to the actual
/// data range (never forced to a fixed 0-100 %).
class TelemetryChart extends StatelessWidget {
  const TelemetryChart({super.key, required this.samples});

  final List<TelemetrySample> samples;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    if (samples.length < 2) {
      return OutlinePanel(
        child: SizedBox(
          height: 180,
          child: Center(child: Text('Chưa đủ dữ liệu để vẽ biểu đồ', style: AcText.body(color: ac.whiteDim))),
        ),
      );
    }

    final values = [for (final s in samples) s.temp];
    var lo = values.reduce(min);
    var hi = values.reduce(max);
    if ((hi - lo).abs() < 1e-6) {
      lo -= 1;
      hi += 1;
    }
    final pad = (hi - lo) * 0.15;
    final minY = lo - pad;
    final maxY = hi + pad;
    final interval = max((maxY - minY) / 4, 0.0001);

    final spots = [for (var i = 0; i < values.length; i++) FlSpot(i.toDouble(), values[i])];

    return OutlinePanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('NHIỆT ĐỘ (°C)', style: AcText.label(color: ac.whiteDim)),
          const SizedBox(height: 14),
          SizedBox(
            height: 200,
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
                      reservedSize: 40,
                      getTitlesWidget: (v, m) => Text(v.toStringAsFixed(1), style: AcText.mono(size: 8, color: ac.whiteDim)),
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
                        final i = v.round().clamp(0, samples.length - 1);
                        return Text(DateFormat('HH:mm').format(samples[i].ts), style: AcText.label(size: 8, color: ac.whiteDim));
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
                          '${t.y.toStringAsFixed(1)}°C\n${DateFormat('HH:mm:ss').format(samples[t.spotIndex.clamp(0, samples.length - 1)].ts)}',
                          AcText.mono(size: 11, color: ac.ice),
                        ),
                    ],
                  ),
                ),
                lineBarsData: [
                  LineChartBarData(
                    spots: spots,
                    isCurved: false,
                    color: ac.ice,
                    barWidth: 1.6,
                    dotData: const FlDotData(show: false),
                    belowBarData: BarAreaData(show: true, color: ac.ice.withValues(alpha: 0.08)),
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
