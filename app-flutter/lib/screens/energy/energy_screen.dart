import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../models/energy_overview.dart';
import '../../models/energy_series.dart';
import '../../state/app_state.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/empty_value.dart';
import '../../widgets/icon_badge.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/section_label.dart';
import 'watt_chart.dart';

/// Full ENERGY screen reached from the dashboard power card: a date-range
/// selector, a Watt(W) line chart for the household, the window's total kWh,
/// and a per-device kWh breakdown. Every figure degrades to "chưa có dữ liệu"
/// when the backend has nothing — never a fabricated number.
class EnergyScreen extends StatefulWidget {
  const EnergyScreen({super.key});

  @override
  State<EnergyScreen> createState() => _EnergyScreenState();
}

enum _Range { today, week, month }

extension _RangeInfo on _Range {
  String get label => switch (this) {
        _Range.today => 'Hôm nay',
        _Range.week => '7 ngày',
        _Range.month => '30 ngày',
      };

  ({DateTime start, DateTime end}) window() {
    final now = DateTime.now();
    return switch (this) {
      _Range.today => (start: DateTime(now.year, now.month, now.day), end: now),
      _Range.week => (start: now.subtract(const Duration(days: 7)), end: now),
      _Range.month => (start: now.subtract(const Duration(days: 30)), end: now),
    };
  }
}

class _EnergyScreenState extends State<EnergyScreen> {
  _Range _range = _Range.today;
  EnergySeries _series = EnergySeries.empty;
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    // Refresh today's per-device overview and load the default window series.
    context.read<AppState>().refreshEnergy();
    _loadSeries();
  }

  Future<void> _loadSeries() async {
    setState(() => _loading = true);
    final w = _range.window();
    final series = await context.read<AppState>().energyApi.series(start: w.start, end: w.end);
    if (mounted) {
      setState(() {
        _series = series;
        _loading = false;
      });
    }
  }

  void _selectRange(_Range r) {
    if (r == _range) return;
    setState(() => _range = r);
    _loadSeries();
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final overview = context.watch<AppState>().energy;

    return Scaffold(
      appBar: AppBar(title: const Text('Điện năng tiêu thụ')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          SegmentedButton<_Range>(
            showSelectedIcon: false,
            style: SegmentedButton.styleFrom(
              foregroundColor: ac.whiteDim,
              selectedForegroundColor: ac.ice,
              selectedBackgroundColor: ac.iceDim,
              side: BorderSide(color: ac.carbonLine),
            ),
            segments: [for (final r in _Range.values) ButtonSegment(value: r, label: Text(r.label))],
            selected: {_range},
            onSelectionChanged: (s) => _selectRange(s.first),
          ),
          const SizedBox(height: 16),
          _totalCard(ac),
          const SizedBox(height: 14),
          if (_loading)
            const OutlinePanel(
              child: SizedBox(height: 200, child: Center(child: CircularProgressIndicator())),
            )
          else
            WattChart(series: _series),
          const SizedBox(height: 22),
          const SectionLabel('Theo thiết bị (hôm nay)'),
          const SizedBox(height: 10),
          _deviceBreakdown(ac, overview),
        ],
      ),
    );
  }

  Widget _totalCard(AcPalette ac) {
    final total = _series.totalKwh;
    return OutlinePanel(
      padding: const EdgeInsets.all(18),
      child: Row(
        children: [
          AcIconBadge(icon: Icons.bolt_outlined, size: 44, color: ac.warning),
          const SizedBox(width: 14),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('Tổng tiêu thụ · ${_range.label}', style: AcText.body(size: 12.5, color: ac.whiteDim)),
                const SizedBox(height: 4),
                Row(
                  crossAxisAlignment: CrossAxisAlignment.baseline,
                  textBaseline: TextBaseline.alphabetic,
                  children: [
                    EmptyValue(total?.toStringAsFixed(2), style: AcText.hero(size: 30, color: ac.white)),
                    const SizedBox(width: 6),
                    Text('kWh', style: AcText.body(size: 13, color: ac.whiteDim)),
                  ],
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _deviceBreakdown(AcPalette ac, EnergyOverview? overview) {
    final devices = overview?.devices ?? const <DeviceEnergy>[];
    if (devices.isEmpty) {
      return OutlinePanel(
        child: Text('Chưa có dữ liệu điện theo thiết bị', style: AcText.body(size: 12.5, color: ac.whiteDim)),
      );
    }
    return Column(
      children: [
        for (final d in devices) ...[
          _DeviceEnergyCard(device: d),
          const SizedBox(height: 10),
        ],
      ],
    );
  }
}

class _DeviceEnergyCard extends StatelessWidget {
  const _DeviceEnergyCard({required this.device});
  final DeviceEnergy device;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final pct = device.pctVsYesterday;
    return OutlinePanel(
      child: Row(
        children: [
          AcIconBadge(icon: Icons.ac_unit, size: 42),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(device.name, style: AcText.heading(size: 14, color: ac.white)),
                if (pct != null) ...[
                  const SizedBox(height: 2),
                  Text(
                    '${pct >= 0 ? '+' : ''}${pct.toStringAsFixed(0)}% so với hôm qua',
                    style: AcText.body(size: 11.5, color: pct >= 0 ? ac.warning : ac.success),
                  ),
                ],
              ],
            ),
          ),
          Row(
            crossAxisAlignment: CrossAxisAlignment.baseline,
            textBaseline: TextBaseline.alphabetic,
            children: [
              EmptyValue(device.kwhToday?.toStringAsFixed(2), style: AcText.mono(size: 16, color: ac.iceText)),
              const SizedBox(width: 4),
              Text('kWh', style: AcText.label(size: 10, color: ac.whiteDim)),
            ],
          ),
        ],
      ),
    );
  }
}
