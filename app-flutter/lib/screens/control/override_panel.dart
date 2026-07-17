import 'package:flutter/material.dart';

import '../../models/ac_mode.dart';
import '../../models/config_bounds.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../widgets/outline_panel.dart';
import '../../widgets/primary_button.dart';
import '../../widgets/section_label.dart';

/// Manual override: pick mode + setpoint, submit.
///
/// CLAMP BOUNDS (brief rule #2 — do not hardcode [16,30]): when [bounds] is
/// non-null (server confirmed them via the owner-only `/configs`, see
/// AppState._loadBounds), the slider is constrained to the REAL server
/// range. When [bounds] is null (role=member, 403, or not loaded yet) there
/// is no honest range to show — the slider is disabled and a plain
/// increment/decrement stepper is offered instead with NO invented min/max;
/// the server's actual rejection (422 "setpoint must be within [...]") is
/// what tells the user the real bounds if they overshoot.
class OverridePanel extends StatefulWidget {
  const OverridePanel({
    super.key,
    required this.bounds,
    required this.onSubmit,
    this.initialMode = AcMode.cool,
    this.initialSetpoint = 25,
  });

  final ConfigBounds? bounds;
  final Future<String?> Function(AcMode mode, int setpoint) onSubmit;
  final AcMode initialMode;
  final int initialSetpoint;

  @override
  State<OverridePanel> createState() => _OverridePanelState();
}

class _OverridePanelState extends State<OverridePanel> {
  late AcMode _mode = widget.initialMode;
  late int _setpoint = widget.initialSetpoint;
  bool _busy = false;
  String? _message;

  Future<void> _submit() async {
    setState(() {
      _busy = true;
      _message = null;
    });
    final err = await widget.onSubmit(_mode, _setpoint);
    if (mounted) setState(() {
      _busy = false;
      _message = err;
    });
  }

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final b = widget.bounds;
    return OutlinePanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const SectionLabel('Ghi đè thủ công'),
          const SizedBox(height: 12),
          Wrap(
            spacing: 8,
            runSpacing: 8,
            children: [
              for (final m in AcMode.values)
                ChoiceChip(
                  label: Text(m.label, style: AcText.label(size: 11)),
                  selected: _mode == m,
                  onSelected: _busy ? null : (_) => setState(() => _mode = m),
                ),
            ],
          ),
          const SizedBox(height: 16),
          Row(
            children: [
              Text('Nhiệt độ đặt', style: AcText.body(size: 12.5, color: ac.whiteDim)),
              const Spacer(),
              Text('$_setpoint°C', style: AcText.mono(size: 16, color: ac.ice)),
            ],
          ),
          if (b != null) ...[
            Slider(
              value: _setpoint.toDouble().clamp(b.clampMin, b.clampMax),
              min: b.clampMin,
              max: b.clampMax,
              divisions: (b.clampMax - b.clampMin).round().clamp(1, 100),
              onChanged: _busy ? null : (v) => setState(() => _setpoint = v.round()),
            ),
            Text('Phạm vi cho phép: ${b.clampMin.toStringAsFixed(0)}–${b.clampMax.toStringAsFixed(0)}°C',
                style: AcText.label(size: 10, color: ac.whiteDim)),
          ] else ...[
            // No confirmed server bounds available (role=member, or not loaded) —
            // offer a plain +/- stepper instead of guessing a range.
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                IconButton(onPressed: _busy ? null : () => setState(() => _setpoint--), icon: const Icon(Icons.remove)),
                const SizedBox(width: 12),
                Text('$_setpoint°C', style: AcText.hero(size: 28, color: ac.white)),
                const SizedBox(width: 12),
                IconButton(onPressed: _busy ? null : () => setState(() => _setpoint++), icon: const Icon(Icons.add)),
              ],
            ),
            Text(
              'Không thể xác nhận giới hạn thật từ máy chủ (cần quyền chủ sở hữu) — '
              'máy chủ sẽ từ chối nếu vượt phạm vi cho phép.',
              style: AcText.label(size: 10, color: ac.warning),
            ),
          ],
          const SizedBox(height: 12),
          if (_message != null) ...[
            Text(_message!, style: AcText.body(size: 12, color: ac.warning)),
            const SizedBox(height: 8),
          ],
          Align(
            alignment: Alignment.centerRight,
            child: _busy
                ? const Padding(padding: EdgeInsets.all(8), child: CircularProgressIndicator())
                : PrimaryButton(label: 'Áp dụng ghi đè', icon: Icons.bolt, onPressed: _submit),
          ),
        ],
      ),
    );
  }
}
