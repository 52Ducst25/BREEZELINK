import 'dart:async';

import 'package:flutter/material.dart';

import '../../models/ac_mode.dart';
import '../../models/config_bounds.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../theme/chamfer_border.dart';
import '../../widgets/outline_panel.dart';

/// Round AC-remote control.
///
/// - A dedicated POWER button turns the AC on/off. "On" resumes the last active
///   mode (so pressing power after a TẮT brings back what you were running),
///   which is the "mở máy lạnh" the four-mode row was missing.
/// - No "send" button: every press sends its command straight away (temp presses
///   are debounced so holding −/+ fires once, not ten times).
/// - The active mode's icon lights up so you always see which mode is on.
///
/// Device reality (verified against the IR pipeline): mode/setpoint commands
/// replay a LEARNED raw-IR capture keyed by (mode, temp). Fan speed is a
/// separate, OPEN-LOOP control: when [onFanSpeed] is wired, each press blasts
/// one learned FAN_SPEED frame so the AC advances its own fan speed — the cloud
/// can't read the result back, so the on-screen level is only a press-counter.
/// On/off, mode, setpoint and this open-loop fan step are what the remote can
/// change on the AC.
class OverridePanel extends StatefulWidget {
  const OverridePanel({
    super.key,
    required this.bounds,
    required this.onSubmit,
    this.onFanSpeed,
    this.initialMode = AcMode.cool,
    this.initialSetpoint = 25,
  });

  final ConfigBounds? bounds;
  final Future<String?> Function(AcMode mode, int setpoint) onSubmit;

  /// Sends one fan-speed step (blasts the learned FAN_SPEED IR). Returns null
  /// on success, or a message (e.g. the button isn't learned yet). When null,
  /// the fan-speed control is hidden.
  final Future<String?> Function()? onFanSpeed;

  final AcMode initialMode;
  final int initialSetpoint;

  @override
  State<OverridePanel> createState() => _OverridePanelState();
}

enum _SendState { idle, sending, sent, error }

// The mode row is the operating modes only; power (off) is its own button.
const _kModes = [AcMode.cool, AcMode.dry, AcMode.fan];

// Displayed fan-speed cycle (per request: 20%→100%→Tự động→20%). This is a
// press-counter shown in the app — the AC tracks its own real fan speed, which
// this open-loop control cannot read back.
const _kFanLevels = ['20%', '40%', '60%', '80%', '100%', 'Tự động'];

class _OverridePanelState extends State<OverridePanel> {
  late AcMode _mode = widget.initialMode;
  late int _setpoint = widget.initialSetpoint;
  // Remembered so the power button can turn the AC back ON into what it was
  // running before it was turned off. Never OFF.
  late AcMode _lastActiveMode = widget.initialMode == AcMode.off ? AcMode.cool : widget.initialMode;
  // -1 = chưa rõ (chưa bấm lần nào). Only a display counter — see _kFanLevels.
  int _fanIdx = -1;
  _SendState _state = _SendState.idle;
  String? _error;
  Timer? _tempDebounce;
  Timer? _sentReset;

  bool get _on => _mode != AcMode.off;
  int get _min => widget.bounds?.clampMin.round() ?? 16;
  int get _max => widget.bounds?.clampMax.round() ?? 32;

  @override
  void dispose() {
    _tempDebounce?.cancel();
    _sentReset?.cancel();
    super.dispose();
  }

  // A press updates the screen immediately (optimistic) and fires the command.
  // Temp is debounced; power and mode send at once.
  void _step(int delta) {
    if (!_on) return;
    setState(() => _setpoint = (_setpoint + delta).clamp(_min, _max));
    _tempDebounce?.cancel();
    _tempDebounce = Timer(const Duration(milliseconds: 550), _send);
  }

  void _togglePower() {
    setState(() {
      if (_on) {
        _lastActiveMode = _mode; // remember what to resume
        _mode = AcMode.off;
      } else {
        _mode = _lastActiveMode;
      }
    });
    _tempDebounce?.cancel();
    _send();
  }

  void _pickMode(AcMode m) {
    if (m == _mode) return;
    setState(() {
      _mode = m;
      _lastActiveMode = m;
    });
    _tempDebounce?.cancel(); // a pending temp send is superseded by this one
    _send();
  }

  Future<void> _send() async {
    setState(() {
      _state = _SendState.sending;
      _error = null;
    });
    final err = await widget.onSubmit(_mode, _setpoint);
    if (!mounted) return;
    setState(() {
      _state = err == null ? _SendState.sent : _SendState.error;
      _error = err;
    });
    if (err == null) {
      _sentReset?.cancel();
      _sentReset = Timer(const Duration(seconds: 2), () {
        if (mounted) setState(() => _state = _SendState.idle);
      });
    }
  }

  Future<void> _sendFan() async {
    final onFan = widget.onFanSpeed;
    if (onFan == null || !_on) return;
    final prev = _fanIdx;
    setState(() {
      _fanIdx = (_fanIdx + 1) % _kFanLevels.length; // advance the displayed level
      _state = _SendState.sending;
      _error = null;
    });
    final err = await onFan();
    if (!mounted) return;
    setState(() {
      if (err != null) {
        _fanIdx = prev; // not learned / failed — don't pretend the speed changed
        _state = _SendState.error;
        _error = err;
      } else {
        _state = _SendState.sent;
      }
    });
    if (err == null) {
      _sentReset?.cancel();
      _sentReset = Timer(const Duration(seconds: 2), () {
        if (mounted) setState(() => _state = _SendState.idle);
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return OutlinePanel(
      child: Column(
        children: [
          _powerBar(context),
          const SizedBox(height: 16),
          _dial(context),
          const SizedBox(height: 18),
          _modeRow(context),
          if (widget.onFanSpeed != null) ...[
            const SizedBox(height: 12),
            _fanRow(context),
          ],
          const SizedBox(height: 12),
          _status(context),
        ],
      ),
    );
  }

  // ---- power button: the explicit on/off ("mở / tắt máy lạnh") ----
  Widget _powerBar(BuildContext ctx) {
    final ac = ctx.ac;
    final on = _on;
    final tint = on ? ac.success : ac.whiteDim;
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: _togglePower,
        customBorder: const ChamferBorder(cut: 8),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 150),
          padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 16),
          decoration: ShapeDecoration(
            color: on ? ac.success.withValues(alpha: 0.14) : ac.carbonUp,
            shape: ChamferBorder(cut: 8, side: BorderSide(color: tint, width: 2)),
          ),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(Icons.power_settings_new, size: 22, color: tint),
              const SizedBox(width: 10),
              Text('NGUỒN', style: AcText.label(size: 12, color: tint)),
              const SizedBox(width: 10),
              Text('·', style: AcText.label(size: 12, color: tint)),
              const SizedBox(width: 10),
              Text(on ? 'ĐANG BẬT' : 'ĐÃ TẮT',
                  style: AcText.heading(size: 13, color: on ? ac.success : ac.whiteDim)),
            ],
          ),
        ),
      ),
    );
  }

  // ---- round dial: − | temperature circle | + ----
  Widget _dial(BuildContext ctx) {
    final ac = ctx.ac;
    final ringColor = _on ? ac.ice : ac.carbonLine;
    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        _CircleKey(icon: Icons.remove, onTap: _on ? () => _step(-1) : null),
        const SizedBox(width: 20),
        Container(
          width: 150,
          height: 150,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: ac.carbon,
            border: Border.all(color: ringColor, width: 3),
            boxShadow: _on ? [BoxShadow(color: ac.iceGlow, blurRadius: 18)] : null,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (_on) ...[
                // current mode, right inside the dial so the reading and the
                // mode it belongs to are read together
                Icon(_mode.icon, size: 20, color: ac.iceText),
                const SizedBox(height: 2),
                Text('$_setpoint°', style: AcText.hero(size: 52, color: ac.white)),
                Text(
                  widget.bounds != null ? '$_min–$_max°C' : '°C',
                  style: AcText.label(size: 9, color: ac.whiteDim),
                ),
              ] else ...[
                Icon(Icons.power_settings_new, size: 26, color: ac.whiteDim),
                const SizedBox(height: 6),
                Text('ĐÃ TẮT', style: AcText.heading(size: 22, color: ac.whiteDim)),
                Text('Bấm NGUỒN để bật', style: AcText.label(size: 8.5, color: ac.whiteDim)),
              ],
            ],
          ),
        ),
        const SizedBox(width: 20),
        _CircleKey(icon: Icons.add, onTap: _on ? () => _step(1) : null),
      ],
    );
  }

  // ---- mode keys — the active one lights up (icon + fill) ----
  Widget _modeRow(BuildContext ctx) {
    return Row(
      children: [
        for (final m in _kModes) ...[
          Expanded(
            child: _ModeKey(mode: m, selected: _mode == m, onTap: () => _pickMode(m)),
          ),
          if (m != _kModes.last) const SizedBox(width: 8),
        ],
      ],
    );
  }

  // ---- fan-speed stepper: one learned "fan speed" IR button, tapped to cycle.
  // Open-loop — the label is a press counter, the AC owns the real speed. ----
  Widget _fanRow(BuildContext ctx) {
    final ac = ctx.ac;
    final enabled = _on;
    final label = _fanIdx < 0 ? '—' : _kFanLevels[_fanIdx];
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text('TỐC ĐỘ QUẠT', style: AcText.label(size: 10, color: ac.whiteDim)),
        const SizedBox(height: 8),
        Opacity(
          opacity: enabled ? 1 : 0.4,
          child: Material(
            color: Colors.transparent,
            child: InkWell(
              onTap: enabled ? _sendFan : null,
              customBorder: const ChamferBorder(cut: 8),
              child: Container(
                padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 16),
                decoration: ShapeDecoration(
                  color: ac.carbonUp,
                  shape: ChamferBorder(cut: 8, side: BorderSide(color: ac.ice, width: 2)),
                ),
                child: Row(
                  children: [
                    Icon(Icons.air, size: 22, color: ac.ice),
                    const SizedBox(width: 12),
                    Expanded(child: Text(label, style: AcText.mono(size: 18, color: ac.white))),
                    Icon(Icons.rotate_right, size: 18, color: ac.iceText),
                    const SizedBox(width: 6),
                    Text('ĐỔI', style: AcText.label(size: 10, color: ac.iceText)),
                  ],
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }

  // ---- feedback line (there is no send button, so this confirms the press) ----
  Widget _status(BuildContext ctx) {
    final ac = ctx.ac;
    switch (_state) {
      case _SendState.sending:
        return Row(mainAxisAlignment: MainAxisAlignment.center, children: [
          const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2)),
          const SizedBox(width: 8),
          Text('Đang gửi lệnh…', style: AcText.label(size: 11, color: ac.whiteDim)),
        ]);
      case _SendState.sent:
        return Row(mainAxisAlignment: MainAxisAlignment.center, children: [
          Icon(Icons.check_circle, size: 16, color: ac.success),
          const SizedBox(width: 6),
          Text('Đã gửi lệnh', style: AcText.label(size: 11, color: ac.success)),
        ]);
      case _SendState.error:
        return Text(_error ?? 'Gửi lệnh thất bại',
            textAlign: TextAlign.center, style: AcText.body(size: 11.5, color: ac.warning));
      case _SendState.idle:
        return Text('Bấm nút bất kỳ để gửi lệnh ngay',
            textAlign: TextAlign.center, style: AcText.label(size: 10, color: ac.whiteDim));
    }
  }
}

/// A round −/+ key; disabled (dimmed, no tap) when the AC is off.
class _CircleKey extends StatelessWidget {
  const _CircleKey({required this.icon, required this.onTap});
  final IconData icon;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final on = onTap != null;
    return Opacity(
      opacity: on ? 1 : 0.35,
      child: Material(
        color: Colors.transparent,
        shape: const CircleBorder(),
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: onTap,
          customBorder: const CircleBorder(),
          child: Container(
            width: 56,
            height: 56,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: ac.carbonUp,
              border: Border.all(color: ac.ice, width: 2),
            ),
            child: Icon(icon, size: 26, color: ac.ice),
          ),
        ),
      ),
    );
  }
}

/// One mode key — the active mode fills with accent and its icon lights up.
class _ModeKey extends StatelessWidget {
  const _ModeKey({required this.mode, required this.selected, required this.onTap});
  final AcMode mode;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final fg = selected ? Colors.white : ac.whiteDim;
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        customBorder: const ChamferBorder(cut: 8),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 150),
          padding: const EdgeInsets.symmetric(vertical: 12),
          decoration: ShapeDecoration(
            color: selected ? ac.ice : ac.carbonUp,
            shape: ChamferBorder(
              cut: 8,
              side: BorderSide(color: selected ? ac.ice : ac.carbonLine, width: 2),
            ),
            shadows: selected ? [BoxShadow(color: ac.iceGlow, blurRadius: 12)] : null,
          ),
          child: Column(
            children: [
              Icon(mode.icon, size: 24, color: fg),
              const SizedBox(height: 6),
              Text(_short(mode), textAlign: TextAlign.center, style: AcText.label(size: 9.5, color: fg)),
            ],
          ),
        ),
      ),
    );
  }

  static String _short(AcMode m) => switch (m) {
        AcMode.cool => 'LẠNH',
        AcMode.dry => 'HÚT ẨM',
        AcMode.fan => 'QUẠT',
        AcMode.off => 'TẮT',
      };
}
