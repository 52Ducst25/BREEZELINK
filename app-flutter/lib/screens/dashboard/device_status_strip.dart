import 'package:flutter/material.dart';

import '../../models/device.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/status_dot.dart';

/// Compact horizontal strip of device online/offline chips for the dashboard
/// summary (full detail lives on the THIẾT BỊ tab).
class DeviceStatusStrip extends StatelessWidget {
  const DeviceStatusStrip({super.key, required this.devices});

  final List<Device> devices;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    if (devices.isEmpty) {
      return Text('Chưa có thiết bị nào', style: AcText.body(size: 12, color: ac.whiteDim));
    }
    return Wrap(
      spacing: 10,
      runSpacing: 8,
      children: [
        for (final d in devices)
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
            decoration: BoxDecoration(color: ac.carbonUp, border: Border.all(color: ac.carbonLine, width: 2)),
            child: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                StatusDot(
                  size: 8,
                  color: d.status == DeviceOnlineStatus.online ? ac.success : ac.whiteDim,
                ),
                const SizedBox(width: 8),
                Text(d.name, style: AcText.mono(size: 12, color: ac.white)),
              ],
            ),
          ),
      ],
    );
  }
}
