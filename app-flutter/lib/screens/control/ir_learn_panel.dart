import 'package:flutter/material.dart';

import '../../models/ac_mode.dart';
import '../../models/ir_code.dart';
import '../../services/ir_api.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/primary_button.dart';
import '../../widgets/section_label.dart';

/// LEARN flow: point the physical remote at the indoor node's IR receiver,
/// pick the button being taught, trigger LEARN, then refresh coverage.
/// The capture itself happens asynchronously over MQTT (202 response has no
/// payload) — this panel just triggers it and lets the user re-check
/// coverage once they've pressed the remote button.
class IrLearnPanel extends StatefulWidget {
  const IrLearnPanel({super.key, required this.irApi});

  final IrApi irApi;

  @override
  State<IrLearnPanel> createState() => _IrLearnPanelState();
}

class _IrLearnPanelState extends State<IrLearnPanel> {
  AcMode _mode = AcMode.cool;
  int _temp = 24;
  IrCoverage? _coverage;
  bool _busy = false;
  String? _status;

  @override
  void initState() {
    super.initState();
    _refreshCoverage();
  }

  Future<void> _refreshCoverage() async {
    try {
      final c = await widget.irApi.coverage();
      if (mounted) setState(() => _coverage = c);
    } catch (_) {
      // Non-fatal — the coverage list just stays as-is (or unknown at first load).
    }
  }

  Future<void> _trigger() async {
    setState(() {
      _busy = true;
      _status = null;
    });
    try {
      await widget.irApi.triggerLearn(mode: _mode, temp: _mode == AcMode.cool ? _temp : null);
      if (mounted) {
        setState(() => _status = 'Đã gửi lệnh LEARN — bấm nút tương ứng trên điều khiển thật trong vài giây.');
      }
    } catch (e) {
      if (mounted) setState(() => _status = 'Lỗi: $e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final coverage = _coverage;
    return OutlinePanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SectionLabel('Học mã lệnh hồng ngoại (LEARN)'),
          const SizedBox(height: 12),
          Wrap(
            spacing: 8,
            children: [
              for (final m in AcMode.values)
                ChoiceChip(
                  label: Text(m.label, style: AcText.label(size: 11)),
                  selected: _mode == m,
                  onSelected: _busy ? null : (_) => setState(() => _mode = m),
                ),
            ],
          ),
          if (_mode == AcMode.cool) ...[
            const SizedBox(height: 10),
            Row(
              children: [
                Text('Nhiệt độ', style: AcText.body(size: 12, color: ac.whiteDim)),
                const Spacer(),
                IconButton(onPressed: _busy ? null : () => setState(() => _temp--), icon: const Icon(Icons.remove, size: 18)),
                Text('$_temp°C', style: AcText.mono(size: 14, color: ac.white)),
                IconButton(onPressed: _busy ? null : () => setState(() => _temp++), icon: const Icon(Icons.add, size: 18)),
              ],
            ),
          ],
          const SizedBox(height: 12),
          Row(
            children: [
              PrimaryButton(label: 'Bắt đầu LEARN', icon: Icons.settings_remote, onPressed: _busy ? null : _trigger),
              const SizedBox(width: 10),
              PrimaryButton(label: 'Làm mới', primary: false, onPressed: _refreshCoverage),
            ],
          ),
          if (_status != null) ...[
            const SizedBox(height: 10),
            Text(_status!, style: AcText.body(size: 12, color: ac.ice)),
          ],
          const SizedBox(height: 16),
          Divider(color: ac.carbonLine),
          const SizedBox(height: 10),
          if (coverage == null)
            Text('Đang tải danh sách mã đã học…', style: AcText.body(size: 12, color: ac.whiteDim))
          else ...[
            Text(
              coverage.isComplete ? 'Đã đủ mã lệnh — tự động điều khiển sẵn sàng.' : 'Còn thiếu: ${coverage.missing.join(', ')}',
              style: AcText.body(size: 12, color: coverage.isComplete ? ac.success : ac.warning),
            ),
            const SizedBox(height: 8),
            for (final code in coverage.codes)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 3),
                child: Row(
                  children: [
                    Icon(Icons.check_circle_outline, size: 14, color: ac.success),
                    const SizedBox(width: 8),
                    Text(code.buttonLabel, style: AcText.mono(size: 12, color: ac.white)),
                  ],
                ),
              ),
          ],
        ],
      ),
    );
  }
}
