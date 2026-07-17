import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../models/comfort_preview.dart';
import '../../state/app_state.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/empty_value.dart';
import '../../widgets/mode_chip.dart';
import '../../widgets/offline_banner.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/section_label.dart';
import '../../widgets/status_dot.dart';
import 'comfort_pipeline_card.dart';
import 'device_status_strip.dart';
import 'reading_tile.dart';

/// TRẠNG THÁI tab: the comfort dashboard — big current decision (t_set +
/// mode), indoor/outdoor tiles, device status, and the full pipeline
/// read-out so the decision is auditable rather than a black box.
class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final s = context.watch<AppState>();
    final comfort = s.comfort;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Row(
          children: [
            StatusDot(size: 9, color: s.wsConnected ? context.ac.success : context.ac.warning),
            const SizedBox(width: 10),
            Expanded(
              child: Text('Trạng thái điều hòa', style: AcText.heading(size: 18, color: context.ac.white)),
            ),
          ],
        ),
        const SizedBox(height: 4),
        Padding(
          padding: const EdgeInsets.only(left: 19),
          child: Text(
            s.wsConnected ? 'TRỰC TUYẾN' : 'ĐANG KẾT NỐI LẠI…',
            style: AcText.body(size: 12, color: context.ac.whiteDim),
          ),
        ),
        const SizedBox(height: 16),
        if (!s.wsConnected) ...[const OfflineBanner(), const SizedBox(height: 16)],
        _HeroSetpoint(comfort: comfort),
        const SizedBox(height: 16),
        Row(
          children: [
            Expanded(child: ReadingTile(label: 'Trong nhà', reading: s.indoor, neutral: comfort.tNeutral)),
            const SizedBox(width: 12),
            Expanded(child: ReadingTile(label: 'Ngoài trời', reading: s.outdoor, neutral: comfort.tNeutral)),
          ],
        ),
        const SizedBox(height: 20),
        const SectionLabel('Thiết bị'),
        const SizedBox(height: 10),
        DeviceStatusStrip(devices: s.devices),
        const SizedBox(height: 20),
        ComfortPipelineCard(comfort: comfort),
      ],
    );
  }
}

/// Large t_set + mode readout — the "what the system is doing right now"
/// hero. Em-dash when the server has no decision yet (data_available=false
/// or IR table not learned) — never a fabricated number.
class _HeroSetpoint extends StatelessWidget {
  const _HeroSetpoint({required this.comfort});

  final ComfortPreview comfort;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return OutlinePanel(
      padding: const EdgeInsets.all(20),
      accent: comfort.tSet == null ? null : ac.ice,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Expanded(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.baseline,
              textBaseline: TextBaseline.alphabetic,
              children: [
                EmptyValue(comfort.tSet?.toString(), style: AcText.hero(color: ac.ice)),
                const SizedBox(width: 6),
                Text('°C', style: AcText.body(size: 16, color: ac.whiteDim)),
              ],
            ),
          ),
          ModeChip(mode: comfort.mode),
        ],
      ),
    );
  }
}
