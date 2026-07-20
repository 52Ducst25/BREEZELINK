import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../models/live_reading.dart';
import '../../state/app_state.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/empty_value.dart';
import '../../widgets/icon_badge.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/primary_button.dart';
import 'override_panel.dart';

/// ĐIỀU KHIỂN tab: manual override + IR learn flow, restyled to the BenKon
/// look — a small indoor readings card, then the big circular dial + controls.
///
/// TTL COUNTDOWN GAP (documented, not silently faked): the backend has no
/// GET-current-override endpoint (`redis_override_service` only exposes
/// set/get/clear, no remaining-TTL read) — so a countdown here can only ever
/// be a CLIENT-SIDE ESTIMATE starting from the moment this app successfully
/// posted the override, using `override_hours` from `/configs` (owner-only).
/// For a `member`, that duration is unknown too, so no countdown is shown at
/// all — only an honest "override active, duration managed by the server"
/// note plus the always-available manual "Hủy ghi đè" action.
class ControlScreen extends StatefulWidget {
  const ControlScreen({super.key});

  @override
  State<ControlScreen> createState() => _ControlScreenState();
}

class _ControlScreenState extends State<ControlScreen> {
  DateTime? _overrideSetAt;
  Timer? _tick;

  @override
  void dispose() {
    _tick?.cancel();
    super.dispose();
  }

  void _startLocalCountdown() {
    _overrideSetAt = DateTime.now();
    _tick?.cancel();
    _tick = Timer.periodic(const Duration(seconds: 1), (_) => setState(() {}));
  }

  @override
  Widget build(BuildContext context) {
    final s = context.watch<AppState>();
    final bounds = s.bounds;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        _ReadingsCard(indoor: s.indoor),
        const SizedBox(height: 16),
        if (_overrideSetAt != null) ...[
          _OverrideStatusCard(setAt: _overrideSetAt!, overrideHours: bounds?.overrideHours, onClear: _clear),
          const SizedBox(height: 16),
        ],
        OverridePanel(
          bounds: bounds,
          onSubmit: (mode, setpoint) async {
            final err = await s.setOverride(mode: mode, setpoint: setpoint);
            if (err == null || err.contains('thiếu mã lệnh')) _startLocalCountdown();
            return err;
          },
          // Peripheral remote buttons (fan speed, sleep, eco, …) replay a
          // learned IR frame; returns a "chưa học" message when not yet taught.
          onAction: (wire) => s.sendAction(wire),
        ),
        const SizedBox(height: 8),
      ],
    );
  }

  Future<void> _clear() async {
    final err = await context.read<AppState>().clearOverride();
    if (err == null && mounted) {
      setState(() {
        _overrideSetAt = null;
        _tick?.cancel();
      });
    }
  }
}

/// Small indoor readings card above the dial — Temperature / Humidity, em-dash
/// when there is no telemetry yet.
class _ReadingsCard extends StatelessWidget {
  const _ReadingsCard({required this.indoor});
  final LiveReading? indoor;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return OutlinePanel(
      child: Row(
        children: [
          Expanded(
            child: _metric(ac, Icons.thermostat_outlined, indoor?.temp.toStringAsFixed(1), '°C', 'Nhiệt độ phòng'),
          ),
          Container(width: 1, height: 40, color: ac.carbonLine),
          Expanded(
            child: _metric(ac, Icons.water_drop_outlined, indoor?.humidity.toStringAsFixed(0), '%', 'Độ ẩm phòng'),
          ),
        ],
      ),
    );
  }

  Widget _metric(AcPalette ac, IconData icon, String? value, String unit, String label) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        Icon(icon, size: 20, color: ac.whiteDim),
        const SizedBox(width: 10),
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                EmptyValue(value, style: AcText.mono(size: 20, color: ac.iceText)),
                if (value != null) Text(unit, style: AcText.mono(size: 12, color: ac.whiteDim)),
              ],
            ),
            Text(label, style: AcText.body(size: 11, color: ac.whiteDim)),
          ],
        ),
      ],
    );
  }
}

class _OverrideStatusCard extends StatelessWidget {
  const _OverrideStatusCard({required this.setAt, required this.overrideHours, required this.onClear});

  final DateTime setAt;
  final int? overrideHours;
  final VoidCallback onClear;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    String remainingLabel;
    if (overrideHours != null && overrideHours! > 0) {
      final end = setAt.add(Duration(hours: overrideHours!));
      final remaining = end.difference(DateTime.now());
      remainingLabel = remaining.isNegative
          ? 'Có thể đã hết hạn (ước tính)'
          : 'Còn ước tính ${remaining.inHours}h ${remaining.inMinutes % 60}m';
    } else {
      remainingLabel = 'Thời lượng do máy chủ quản lý (không xác định được từ ứng dụng)';
    }
    return OutlinePanel(
      accent: ac.warning,
      child: Row(
        children: [
          AcIconBadge(icon: Icons.pan_tool_outlined, color: ac.warning, size: 40, iconSize: 20),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('Ghi đè thủ công đang hoạt động', style: AcText.heading(size: 13, color: ac.white)),
                Text(remainingLabel, style: AcText.body(size: 11.5, color: ac.whiteDim)),
              ],
            ),
          ),
          PrimaryButton(label: 'Hủy', primary: false, onPressed: onClear),
        ],
      ),
    );
  }
}
