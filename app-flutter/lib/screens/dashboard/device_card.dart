import 'package:flutter/material.dart';

import '../../models/ac_mode.dart';
import '../../models/device.dart';
import '../../models/live_reading.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/empty_value.dart';
import '../../widgets/icon_badge.dart';
import '../../widgets/mode_chip.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/pill.dart';
import '../../widgets/status_dot.dart';

/// BenKon-style device card for "Thiết bị của bạn": a leading rounded-square
/// icon, the device name, an Online/Offline pill, the room temperature +
/// humidity line, and — for the indoor AC controller node — a bottom row with
/// the current mode pill, the setpoint pill (outlined accent) and an on/off
/// toggle.
///
/// Room readings come from THIS node's live reading; the mode/setpoint pills +
/// power toggle reflect and drive the household's single AC comfort decision
/// (there is one AC — the indoor node carries the IR blaster). Outdoor sensor
/// nodes show readings + status only, never AC controls.
class DeviceCard extends StatelessWidget {
  const DeviceCard({
    super.key,
    required this.device,
    required this.reading,
    required this.mode,
    required this.setpoint,
    required this.onTogglePower,
  });

  final Device device;
  final LiveReading? reading;
  final AcMode? mode;
  final int? setpoint;

  /// Called with the desired power state when the toggle is flipped. Wired to a
  /// manual override on the dashboard; a no-op is fine for read-only contexts.
  final Future<void> Function(bool on) onTogglePower;

  bool get _isController => device.nodeType != NodeType.outdoor;
  bool get _powerOn => mode != null && mode != AcMode.off;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final online = device.status == DeviceOnlineStatus.online;

    return OutlinePanel(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              AcIconBadge(
                icon: device.nodeType == NodeType.outdoor ? Icons.cloud_outlined : Icons.ac_unit,
                size: 42,
                iconSize: 22,
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(device.name, style: AcText.heading(size: 14.5, color: ac.white)),
                    const SizedBox(height: 2),
                    Text(
                      '${device.nodeType.label}${device.location != null ? ' • ${device.location}' : ''}',
                      style: AcText.body(size: 11.5, color: ac.whiteDim),
                    ),
                  ],
                ),
              ),
              AcPill(
                label: online ? 'Online' : 'Offline',
                color: online ? ac.success : ac.whiteDim,
                dense: true,
                leading: StatusDot(size: 7, color: online ? ac.success : ac.whiteDim),
              ),
            ],
          ),
          const SizedBox(height: 14),
          _roomLine(ac),
          if (_isController) ...[
            const SizedBox(height: 14),
            Divider(color: ac.carbonLine, height: 1),
            const SizedBox(height: 12),
            _controlRow(context, ac),
          ],
        ],
      ),
    );
  }

  /// "Nhiệt độ trong nhà: 25.8°C   Độ ẩm trong nhà: 62%" — accent numbers,
  /// em-dash when no reading yet. An outdoor sensor node reads the OUTDOOR
  /// air, so its labels say "ngoài trời" instead of "trong nhà".
  Widget _roomLine(AcPalette ac) {
    final where = device.nodeType == NodeType.outdoor ? 'ngoài trời' : 'trong nhà';
    return Wrap(
      spacing: 18,
      runSpacing: 6,
      children: [
        _kv(ac, 'Nhiệt độ $where', reading?.temp.toStringAsFixed(1), '°C'),
        _kv(ac, 'Độ ẩm $where', reading?.humidity.toStringAsFixed(0), '%'),
      ],
    );
  }

  Widget _kv(AcPalette ac, String label, String? value, String unit) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Text('$label: ', style: AcText.body(size: 12, color: ac.whiteDim)),
        EmptyValue(value, suffix: value == null ? '' : unit, style: AcText.mono(size: 13, color: ac.iceText)),
      ],
    );
  }

  Widget _controlRow(BuildContext context, AcPalette ac) {
    return Row(
      children: [
        ModeChip(mode: mode, dense: true),
        const SizedBox(width: 8),
        AcPill(
          label: setpoint == null ? '—' : '$setpoint°C',
          icon: Icons.thermostat_outlined,
          filled: false,
          dense: true,
          color: ac.ice,
        ),
        const Spacer(),
        Switch(
          value: _powerOn,
          onChanged: (v) => onTogglePower(v),
        ),
      ],
    );
  }
}
