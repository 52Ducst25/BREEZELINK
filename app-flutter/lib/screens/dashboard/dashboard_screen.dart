import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../models/ac_mode.dart';
import '../../models/device.dart';
import '../../state/app_state.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/offline_banner.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/section_label.dart';
import '../../widgets/status_dot.dart';
import '../device/device_history_screen.dart';
import '../energy/energy_screen.dart';
import 'comfort_pipeline_card.dart';
import 'device_card.dart';
import 'location_card.dart';
import 'power_consumption_card.dart';

/// TRẠNG THÁI tab — the BenKon "home": a location card (household + outdoor
/// conditions), a power-consumption card, and the "Thiết bị của bạn" device
/// list. The full comfort pipeline read-out is kept below for auditability.
class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  /// Toggle the household AC on/off via a manual override. Turning on restores
  /// COOL at the last decided setpoint (or 25°C when none is known); turning
  /// off sends OFF. Errors surface as a snackbar — nothing is fabricated.
  Future<void> _togglePower(BuildContext context, bool on) async {
    final s = context.read<AppState>();
    final setpoint = s.comfort.tSet ?? 25;
    final err = await s.setOverride(mode: on ? AcMode.cool : AcMode.off, setpoint: setpoint);
    if (err != null && context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(err)));
    }
  }

  /// Opens one node's temperature/humidity history. The screen takes the API
  /// directly (no Provider) because it is pushed above the tab shell's scope.
  void _openHistory(BuildContext context, Device device) {
    final api = context.read<AppState>().telemetryApi;
    Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => DeviceHistoryScreen(device: device, api: api),
    ));
  }

  void _openEnergy(BuildContext context) {
    final s = context.read<AppState>();
    Navigator.of(context).push(MaterialPageRoute(
      builder: (_) => ChangeNotifierProvider<AppState>.value(
        value: s,
        child: const EnergyScreen(),
      ),
    ));
  }

  @override
  Widget build(BuildContext context) {
    final s = context.watch<AppState>();
    final ac = context.ac;
    final comfort = s.comfort;

    return RefreshIndicator(
      onRefresh: s.refreshEnergy,
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Row(
            children: [
              StatusDot(size: 9, color: s.wsConnected ? ac.success : ac.warning),
              const SizedBox(width: 10),
              Expanded(child: Text('Trạng thái điều hòa', style: AcText.heading(size: 18, color: ac.white))),
            ],
          ),
          const SizedBox(height: 4),
          Padding(
            padding: const EdgeInsets.only(left: 19),
            child: Text(
              s.wsConnected ? 'TRỰC TUYẾN' : 'ĐANG KẾT NỐI LẠI…',
              style: AcText.body(size: 12, color: ac.whiteDim),
            ),
          ),
          const SizedBox(height: 16),
          if (!s.wsConnected) ...[const OfflineBanner(), const SizedBox(height: 16)],

          // Tài khoản nhân viên: org của họ không sở hữu node nào nên app rỗng
          // là ĐÚNG, không phải hỏng. Không nói ra thì người xem sẽ đi tìm lỗi
          // ở chỗ không có lỗi.
          if (s.profile?.isSysadmin == true) ...[
            _StaffNotice(),
            const SizedBox(height: 16),
          ],

          LocationCard(locationName: _locationName(s.profile?.location, s.devices), outdoor: s.outdoor),
          const SizedBox(height: 14),
          PowerConsumptionCard(energy: s.energy, onTap: () => _openEnergy(context)),

          const SizedBox(height: 22),
          const SectionLabel('Thiết bị của bạn'),
          const SizedBox(height: 10),
          if (s.devices.isEmpty)
            Text('Chưa có thiết bị nào', style: AcText.body(size: 12.5, color: ac.whiteDim))
          else
            for (final d in s.devices) ...[
              DeviceCard(
                device: d,
                reading: d.nodeType == NodeType.outdoor ? s.outdoor : s.indoor,
                mode: comfort.mode,
                setpoint: comfort.tSet,
                onTogglePower: (on) => _togglePower(context, on),
                onTap: () => _openHistory(context, d),
              ),
              const SizedBox(height: 10),
            ],

          const SizedBox(height: 12),
          const SectionLabel('Chi tiết quyết định'),
          const SizedBox(height: 10),
          ComfortPipelineCard(comfort: comfort),
        ],
      ),
    );
  }

  /// Best-effort household label: the outdoor node's location, else the first
  /// device's location, else a neutral fallback.
  String _locationName(String? homeAddress, List<Device> devices) {
    // The home address the user set in Account wins; otherwise fall back to an
    // outdoor node's location, any node's location, then a generic label.
    if (homeAddress != null && homeAddress.trim().isNotEmpty) return homeAddress.trim();
    for (final d in devices) {
      if (d.nodeType == NodeType.outdoor && d.location != null && d.location!.isNotEmpty) {
        return d.location!;
      }
    }
    for (final d in devices) {
      if (d.location != null && d.location!.isNotEmpty) return d.location!;
    }
    return 'Nhà của bạn';
  }
}

/// Nhắc nhân viên rằng app này dành cho hộ gia đình.
///
/// Tài khoản nhân viên đăng nhập được (tiện để thử app), nhưng org của họ không
/// sở hữu node nào — nên màn hình trống trơn là ĐÚNG. Không có dòng này thì
/// người xem sẽ tưởng app hỏng và đi tìm lỗi ở chỗ vốn không có lỗi.
class _StaffNotice extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return OutlinePanel(
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(Icons.badge_outlined, size: 18, color: ac.warning),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('Tài khoản nhân viên', style: AcText.heading(size: 13.5, color: ac.white)),
                const SizedBox(height: 3),
                Text(
                  'App này dành cho tài khoản hộ gia đình. Nhà quản trị không có thiết bị '
                  'nào nên màn hình trống là bình thường — dùng trang quản trị trên web '
                  'để xem toàn bộ node đã bán.',
                  style: AcText.body(size: 11.5, color: ac.whiteDim),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
