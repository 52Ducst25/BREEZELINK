import 'dart:math';

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

import '../../models/telemetry_series.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/outline_panel.dart';

/// One line chart of a device's history — used for BOTH temperature and
/// humidity (same shape, different accessor/unit/colour), mirroring the energy
/// screen's WattChart so the two screens feel identical.
///
/// Shows "chưa có dữ liệu" when the window holds fewer than 2 points: a single
/// reading cannot draw a line, and inventing one would misrepresent the range.
class SensorChart extends StatelessWidget {
  const SensorChart({
    super.key,
    required this.title,
    required this.unit,
    required this.series,
    required this.pick,
    required this.color,
    required this.axisFormat,
    this.decimals = 1,
  });

  final String title;
  final String unit;
  final SensorSeries series;

  /// Which reading this chart draws (temp or humidity).
  final double Function(SensorPoint) pick;
  final Color color;

  /// X-axis label format — HH:mm for a single day, dd/MM for longer windows.
  final DateFormat axisFormat;
  final int decimals;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final points = series.points;

    if (series.isEmpty) {
      return OutlinePanel(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(title, style: AcText.label(color: ac.whiteDim)),
            const SizedBox(height: 12),
            SizedBox(
              height: 160,
              child: Center(
                child: Text('Chưa có dữ liệu trong khoảng này',
                    style: AcText.body(size: 12.5, color: ac.whiteDim)),
              ),
            ),
          ],
        ),
      );
    }

    final values = [for (final p in points) pick(p)];
    var lo = values.reduce(min);
    var hi = values.reduce(max);
    if ((hi - lo).abs() < 1e-6) {
      // Flat line (e.g. a stable room): open the window so it doesn't collapse.
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
          Row(
            children: [
              Text(title, style: AcText.label(color: ac.whiteDim)),
              const Spacer(),
              // Latest value in the window — the number people look for first.
              Text('${values.last.toStringAsFixed(decimals)} $unit',
                  style: AcText.mono(size: 13, color: color)),
            ],
          ),
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
                      getTitlesWidget: (v, m) => Text(v.toStringAsFixed(0),
                          style: AcText.mono(size: 8, color: ac.whiteDim)),
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
                        return Text(axisFormat.format(points[i].ts),
                            style: AcText.label(size: 8, color: ac.whiteDim));
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
                          '${t.y.toStringAsFixed(decimals)} $unit\n'
                          '${DateFormat('dd/MM HH:mm').format(points[t.spotIndex.clamp(0, points.length - 1)].ts)}',
                          AcText.mono(size: 11, color: color),
                        ),
                    ],
                  ),
                ),
                lineBarsData: [
                  LineChartBarData(
                    spots: spots,
                    isCurved: true,
                    color: color,
                    barWidth: 2,
                    dotData: const FlDotData(show: false),
                    belowBarData: BarAreaData(show: true, color: color.withValues(alpha: 0.1)),
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
