import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../models/device.dart';
import '../../state/app_state.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/section_label.dart';
import '../../widgets/status_dot.dart';
import 'device_detail_screen.dart';

/// THIẾT BỊ tab: list of outdoor/indoor nodes with online status. Tap a
/// device to see its telemetry history.
class DevicesScreen extends StatelessWidget {
  const DevicesScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final devices = context.watch<AppState>().devices;
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text('Thiết bị', style: AcText.heading(size: 18, color: context.ac.white)),
        const SizedBox(height: 16),
        const SectionLabel('Danh sách nút cảm biến'),
        const SizedBox(height: 10),
        if (devices.isEmpty)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 24),
            child: Center(child: Text('Chưa có thiết bị nào được đăng ký', style: AcText.body(color: context.ac.whiteDim))),
          )
        else
          for (final d in devices) ...[_DeviceCard(device: d), const SizedBox(height: 10)],
      ],
    );
  }
}

class _DeviceCard extends StatelessWidget {
  const _DeviceCard({required this.device});
  final Device device;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final online = device.status == DeviceOnlineStatus.online;
    return InkWell(
      onTap: () => Navigator.of(context).push(
        MaterialPageRoute(builder: (_) => DeviceDetailScreen(device: device)),
      ),
      child: OutlinePanel(
        child: Row(
          children: [
            Icon(
              device.nodeType == NodeType.outdoor ? Icons.cloud_outlined : Icons.home_outlined,
              color: ac.ice,
              size: 22,
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(device.name, style: AcText.heading(size: 14, color: ac.white)),
                  const SizedBox(height: 2),
                  Text(
                    '${device.nodeType.label}${device.location != null ? ' • ${device.location}' : ''}',
                    style: AcText.body(size: 11.5, color: ac.whiteDim),
                  ),
                ],
              ),
            ),
            StatusDot(size: 8, color: online ? ac.success : ac.whiteDim),
            const SizedBox(width: 6),
            Text(online ? 'ONLINE' : 'OFFLINE', style: AcText.label(size: 10, color: online ? ac.success : ac.whiteDim)),
            const SizedBox(width: 4),
            Icon(Icons.chevron_right, color: ac.whiteDim, size: 18),
          ],
        ),
      ),
    );
  }
}
