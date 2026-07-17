import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../models/device.dart';
import '../../models/telemetry_sample.dart';
import '../../state/app_state.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/outline_panel.dart';
import 'telemetry_chart.dart';

/// One device's detail: identity, last-seen, and its telemetry history chart.
class DeviceDetailScreen extends StatefulWidget {
  const DeviceDetailScreen({super.key, required this.device});

  final Device device;

  @override
  State<DeviceDetailScreen> createState() => _DeviceDetailScreenState();
}

class _DeviceDetailScreenState extends State<DeviceDetailScreen> {
  List<TelemetrySample>? _samples;
  String? _error;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    try {
      final samples = await context.read<AppState>().telemetryApi.list(widget.device.id, limit: 200);
      if (mounted) setState(() => _samples = samples);
    } catch (e) {
      if (mounted) setState(() => _error = 'Không tải được lịch sử: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final d = widget.device;
    return Scaffold(
      appBar: AppBar(title: Text(d.name)),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          OutlinePanel(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                _row(ac, 'Loại nút', d.nodeType.label),
                _row(ac, 'Vị trí', d.location ?? '—'),
                _row(ac, 'Trạng thái', d.status == DeviceOnlineStatus.online ? 'Online' : 'Offline'),
                _row(ac, 'Lần cuối online', d.lastSeenAt?.toLocal().toString() ?? '—'),
              ],
            ),
          ),
          const SizedBox(height: 16),
          if (_error != null)
            Text(_error!, style: AcText.body(color: ac.error))
          else if (_samples == null)
            const Center(child: Padding(padding: EdgeInsets.all(24), child: CircularProgressIndicator()))
          else
            TelemetryChart(samples: _samples!),
        ],
      ),
    );
  }

  Widget _row(AcPalette ac, String label, String value) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 6),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(label, style: AcText.body(size: 12.5, color: ac.whiteDim)),
            Text(value, style: AcText.mono(size: 12.5, color: ac.white)),
          ],
        ),
      );
}
