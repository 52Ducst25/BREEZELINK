import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

import '../../models/device.dart';
import '../../models/telemetry_series.dart';
import '../../services/telemetry_api.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/empty_value.dart';
import '../../widgets/icon_badge.dart';
import '../../widgets/outline_panel.dart';
import 'sensor_chart.dart';

/// Per-node history: tap a device card → temperature + humidity charts over
/// Hôm nay / 7 ngày / 30 ngày. Mirrors the energy screen's layout so the two
/// feel like one app.
///
/// [api] is injected rather than read from Provider: this screen is pushed on
/// the root navigator, which sits ABOVE the tab shell's provider scope, so a
/// `context.read<AppState>()` here would throw ProviderNotFound.
class DeviceHistoryScreen extends StatefulWidget {
  const DeviceHistoryScreen({super.key, required this.device, required this.api});

  final Device device;
  final TelemetryApi api;

  @override
  State<DeviceHistoryScreen> createState() => _DeviceHistoryScreenState();
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

  /// A day's worth of points is read by clock time; longer windows by date.
  DateFormat get axisFormat =>
      this == _Range.today ? DateFormat('HH:mm') : DateFormat('dd/MM');
}

class _DeviceHistoryScreenState extends State<DeviceHistoryScreen> {
  _Range _range = _Range.today;
  SensorSeries _series = SensorSeries.empty;
  bool _loading = true;

  bool get _isOutdoor => widget.device.nodeType == NodeType.outdoor;

  /// Same wording as the device card: an outdoor node measures outdoor air.
  String get _where => _isOutdoor ? 'ngoài trời' : 'trong nhà';

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() => _loading = true);
    final w = _range.window();
    // try/finally: whatever happens, the spinner must stop. Letting an error
    // escape here left the screen loading forever with no way out.
    try {
      final s = await widget.api.series(deviceId: widget.device.id, start: w.start, end: w.end);
      if (mounted) setState(() => _series = s);
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  void _selectRange(_Range r) {
    if (r == _range) return;
    setState(() => _range = r);
    _load();
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;

    return Scaffold(
      appBar: AppBar(title: Text(widget.device.name)),
      body: RefreshIndicator(
        onRefresh: _load,
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            _header(ac),
            const SizedBox(height: 16),
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
            if (_loading)
              const OutlinePanel(
                child: SizedBox(height: 460, child: Center(child: CircularProgressIndicator())),
              )
            else ...[
              _minMaxCard(ac),
              const SizedBox(height: 14),
              SensorChart(
                title: 'NHIỆT ĐỘ ${_where.toUpperCase()} (°C)',
                unit: '°C',
                series: _series,
                pick: (p) => p.temp,
                color: ac.ice,
                axisFormat: _range.axisFormat,
              ),
              const SizedBox(height: 14),
              SensorChart(
                title: 'ĐỘ ẨM ${_where.toUpperCase()} (%)',
                unit: '%',
                series: _series,
                pick: (p) => p.humidity,
                color: ac.success,
                axisFormat: _range.axisFormat,
                decimals: 0,
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _header(AcPalette ac) => OutlinePanel(
        child: Row(
          children: [
            AcIconBadge(
              icon: _isOutdoor ? Icons.cloud_outlined : Icons.ac_unit,
              size: 44,
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(widget.device.nodeType.label, style: AcText.heading(size: 14, color: ac.white)),
                  const SizedBox(height: 2),
                  Text(
                    widget.device.location?.isNotEmpty == true
                        ? widget.device.location!
                        : 'Chưa đặt vị trí',
                    style: AcText.body(size: 11.5, color: ac.whiteDim),
                  ),
                ],
              ),
            ),
          ],
        ),
      );

  /// Min/max over the selected window — the two numbers a range view exists to
  /// answer ("how hot did it get?"). Em-dash when the window has no readings.
  Widget _minMaxCard(AcPalette ac) {
    final t = _series.range((p) => p.temp);
    final h = _series.range((p) => p.humidity);
    return OutlinePanel(
      padding: const EdgeInsets.all(16),
      child: Row(
        children: [
          Expanded(child: _stat(ac, 'Nhiệt độ ${_range.label.toLowerCase()}', t?.min, t?.max, '°C', 1, ac.ice)),
          Container(width: 1, height: 42, color: ac.carbonLine),
          Expanded(child: _stat(ac, 'Độ ẩm ${_range.label.toLowerCase()}', h?.min, h?.max, '%', 0, ac.success)),
        ],
      ),
    );
  }

  Widget _stat(AcPalette ac, String label, double? lo, double? hi, String unit, int dec, Color color) => Padding(
        padding: const EdgeInsets.symmetric(horizontal: 8),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(label, style: AcText.body(size: 11.5, color: ac.whiteDim)),
            const SizedBox(height: 6),
            Row(
              children: [
                Text('Thấp ', style: AcText.label(size: 9, color: ac.whiteDim)),
                EmptyValue(lo?.toStringAsFixed(dec),
                    suffix: lo == null ? '' : unit, style: AcText.mono(size: 13, color: color)),
              ],
            ),
            const SizedBox(height: 2),
            Row(
              children: [
                Text('Cao  ', style: AcText.label(size: 9, color: ac.whiteDim)),
                EmptyValue(hi?.toStringAsFixed(dec),
                    suffix: hi == null ? '' : unit, style: AcText.mono(size: 13, color: color)),
              ],
            ),
          ],
        ),
      );
}
