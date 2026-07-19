import 'dart:async';

import 'package:flutter/material.dart';

import '../../models/ac_action.dart';
import '../../models/ac_mode.dart';
import '../../models/config_bounds.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_text.dart';
import '../../theme/chamfer_border.dart';
import '../../widgets/outline_panel.dart';

/// The AC remote, laid out like a physical handset: a green LCD screen on top,
/// then the button grid — TEMP ▲/▼, POWER, MODE and every peripheral key
/// (SUPER, SLEEP, ECO, QUIET, SMART, TIMER, đảo gió, đèn, °C/°F).
///
/// Two command paths:
///  - POWER / TEMP / MODE change the operating (mode, setpoint) via [onSubmit]
///    (a manual override) — these always work.
///  - Every other key replays ONE learned IR frame via [onAction]. If that
///    button hasn't been learned yet the server says so and the status line
///    tells the user to go learn it — no key is ever silently dead.
///
/// Every press sends immediately (temp is debounced so holding ▲ fires once).
class OverridePanel extends StatefulWidget {
  const OverridePanel({
    super.key,
    required this.bounds,
    required this.onSubmit,
    required this.onAction,
    this.initialMode = AcMode.cool,
    this.initialSetpoint = 25,
  });

  final ConfigBounds? bounds;
  final Future<String?> Function(AcMode mode, int setpoint) onSubmit;
  final Future<String?> Function(String action) onAction;
  final AcMode initialMode;
  final int initialSetpoint;

  @override
  State<OverridePanel> createState() => _OverridePanelState();
}

enum _SendState { idle, sending, sent, error }

const _kModes = [AcMode.cool, AcMode.dry, AcMode.fan];
AcAction _act(String wire) => kAcActions.firstWhere((a) => a.wire == wire);

class _OverridePanelState extends State<OverridePanel> {
  late AcMode _mode = widget.initialMode;
  late int _setpoint = widget.initialSetpoint;
  late AcMode _lastActiveMode = widget.initialMode == AcMode.off ? AcMode.cool : widget.initialMode;
  _SendState _state = _SendState.idle;
  String? _statusMsg;
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

  void _showSending() => setState(() {
        _state = _SendState.sending;
        _statusMsg = null;
      });

  void _showResult(String? err, {String? okMsg}) {
    if (!mounted) return;
    setState(() {
      _state = err == null ? _SendState.sent : _SendState.error;
      _statusMsg = err ?? okMsg;
    });
    if (err == null) {
      _sentReset?.cancel();
      _sentReset = Timer(const Duration(seconds: 2), () {
        if (mounted) setState(() => _state = _SendState.idle);
      });
    }
  }

  // ---- (mode, setpoint) commands ----
  Future<void> _sendOverride() async {
    _showSending();
    _showResult(await widget.onSubmit(_mode, _setpoint));
  }

  void _togglePower() {
    setState(() {
      if (_on) {
        _lastActiveMode = _mode;
        _mode = AcMode.off;
      } else {
        _mode = _lastActiveMode;
      }
    });
    _tempDebounce?.cancel();
    _sendOverride();
  }

  void _cycleMode() {
    setState(() {
      _mode = _on ? _kModes[(_kModes.indexOf(_mode) + 1) % _kModes.length] : AcMode.cool;
      _lastActiveMode = _mode;
    });
    _tempDebounce?.cancel();
    _sendOverride();
  }

  void _step(int delta) {
    if (!_on) return;
    setState(() => _setpoint = (_setpoint + delta).clamp(_min, _max));
    _tempDebounce?.cancel();
    _tempDebounce = Timer(const Duration(milliseconds: 500), _sendOverride);
  }

  // ---- peripheral action commands ----
  Future<void> _action(String wire) async {
    _showSending();
    final err = await widget.onAction(wire);
    _showResult(err, okMsg: 'Đã gửi ${_act(wire).label}');
  }

  @override
  Widget build(BuildContext context) {
    return OutlinePanel(
      child: Column(
        children: [
          _lcd(context),
          const SizedBox(height: 18),
          _TempKey(up: true, onTap: _on ? () => _step(1) : null),
          const SizedBox(height: 10),
          Row(
            children: [
              Expanded(child: _Key(action: _act('SUPER'), onTap: () => _action('SUPER'))),
              const SizedBox(width: 10),
              _PowerKey(on: _on, onTap: _togglePower),
              const SizedBox(width: 10),
              Expanded(child: _Key(action: _act('FAN_SPEED'), onTap: () => _action('FAN_SPEED'))),
            ],
          ),
          const SizedBox(height: 10),
          _TempKey(up: false, onTap: _on ? () => _step(-1) : null),
          const SizedBox(height: 14),
          // 3-column grid of the remaining keys, matching the handset.
          _row3(
            _Key(action: _act('SLEEP'), onTap: () => _action('SLEEP')),
            _Key(label: 'CHẾ ĐỘ', icon: _mode.icon, onTap: _cycleMode, accent: true),
            _Key(action: _act('ECO'), onTap: () => _action('ECO')),
          ),
          const SizedBox(height: 10),
          _row3(
            _Key(action: _act('QUIET'), onTap: () => _action('QUIET')),
            _Key(action: _act('LIGHT'), onTap: () => _action('LIGHT')),
            _Key(action: _act('SWING_V'), onTap: () => _action('SWING_V')),
          ),
          const SizedBox(height: 10),
          _row3(
            _Key(action: _act('SMART'), onTap: () => _action('SMART')),
            _Key(action: _act('TIMER'), onTap: () => _action('TIMER')),
            _Key(action: _act('SWING_H'), onTap: () => _action('SWING_H')),
          ),
          const SizedBox(height: 10),
          _Key(action: _act('TEMP_UNIT'), onTap: () => _action('TEMP_UNIT')),
          const SizedBox(height: 14),
          _status(context),
        ],
      ),
    );
  }

  Widget _row3(Widget a, Widget b, Widget c) => Row(
        children: [
          Expanded(child: a),
          const SizedBox(width: 10),
          Expanded(child: b),
          const SizedBox(width: 10),
          Expanded(child: c),
        ],
      );

  // ---- the green LCD screen ----
  Widget _lcd(BuildContext ctx) {
    // Deliberately its own colour world (not the app's blue): it mimics the
    // physical remote's backlit green LCD so the handset reads as a handset.
    const lcdBg = Color(0xFF12251A);
    const lcdInk = Color(0xFF8BE9A8);
    const lcdDim = Color(0xFF3F6E52);
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 16),
      decoration: BoxDecoration(
        color: lcdBg,
        border: Border.all(color: const Color(0xFF1E3A28), width: 2),
      ),
      child: Row(
        children: [
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(children: [
                Icon(_mode.icon, size: 16, color: _on ? lcdInk : lcdDim),
                const SizedBox(width: 6),
                Text(_on ? _mode.label.toUpperCase() : 'TẮT',
                    style: AcText.label(size: 11, color: _on ? lcdInk : lcdDim)),
              ]),
            ],
          ),
          const Spacer(),
          // Setpoint number only for COOL (DRY/FAN ignore it — the server snaps
          // it away); those show the mode name, off shows a blank readout.
          if (!_on)
            Text('— —', style: AcText.mono(size: 40, color: lcdDim))
          else if (_mode == AcMode.cool)
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('$_setpoint', style: AcText.mono(size: 48, color: lcdInk)),
                Padding(
                  padding: const EdgeInsets.only(top: 8, left: 2),
                  child: Text('°C', style: AcText.mono(size: 18, color: lcdInk)),
                ),
              ],
            )
          else
            Text(_mode.label.toUpperCase(), style: AcText.mono(size: 26, color: lcdInk)),
        ],
      ),
    );
  }

  Widget _status(BuildContext ctx) {
    final ac = ctx.ac;
    switch (_state) {
      case _SendState.sending:
        return _statusLine(const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2)),
            'Đang gửi lệnh…', ac.whiteDim);
      case _SendState.sent:
        return _statusLine(Icon(Icons.check_circle, size: 16, color: ac.success), _statusMsg ?? 'Đã gửi', ac.success);
      case _SendState.error:
        return Text(_statusMsg ?? 'Gửi lệnh thất bại',
            textAlign: TextAlign.center, style: AcText.body(size: 12, color: ac.warning));
      case _SendState.idle:
        return Text('Bấm nút bất kỳ để điều khiển',
            textAlign: TextAlign.center, style: AcText.label(size: 10, color: ac.whiteDim));
    }
  }

  Widget _statusLine(Widget lead, String text, Color color) => Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [lead, const SizedBox(width: 8), Text(text, style: AcText.label(size: 11, color: color))],
      );
}

/// A standard remote key: an icon over a short label, chamfered like the rest of
/// the UI. [accent] outlines it in the brand blue (used for the MODE key).
class _Key extends StatelessWidget {
  const _Key({this.action, this.label, this.icon, required this.onTap, this.accent = false});
  final AcAction? action;
  final String? label;
  final IconData? icon;
  final VoidCallback onTap;
  final bool accent;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final ico = icon ?? action?.icon ?? Icons.circle;
    final txt = label ?? action?.label ?? '';
    final line = accent ? ac.ice : ac.carbonLine;
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        customBorder: const ChamferBorder(cut: 8),
        child: Container(
          padding: const EdgeInsets.symmetric(vertical: 11, horizontal: 6),
          decoration: ShapeDecoration(
            color: ac.carbonUp,
            shape: ChamferBorder(cut: 8, side: BorderSide(color: line, width: 2)),
          ),
          child: Column(
            children: [
              Icon(ico, size: 20, color: accent ? ac.ice : ac.white),
              const SizedBox(height: 5),
              Text(txt, textAlign: TextAlign.center, maxLines: 1,
                  overflow: TextOverflow.ellipsis, style: AcText.label(size: 8.5, color: ac.whiteDim)),
            ],
          ),
        ),
      ),
    );
  }
}

/// Wide TEMP ▲/▼ bar. Disabled (dimmed) when the AC is off.
class _TempKey extends StatelessWidget {
  const _TempKey({required this.up, required this.onTap});
  final bool up;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    final on = onTap != null;
    return Opacity(
      opacity: on ? 1 : 0.4,
      child: Material(
        color: Colors.transparent,
        child: InkWell(
          onTap: onTap,
          customBorder: const ChamferBorder(cut: 8),
          child: Container(
            width: double.infinity,
            padding: const EdgeInsets.symmetric(vertical: 12),
            decoration: ShapeDecoration(
              color: ac.carbonUp,
              shape: ChamferBorder(cut: 8, side: BorderSide(color: ac.carbonLine, width: 2)),
            ),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(up ? Icons.keyboard_arrow_up : Icons.keyboard_arrow_down, size: 22, color: ac.ice),
                const SizedBox(width: 8),
                Text(up ? 'TĂNG NHIỆT' : 'GIẢM NHIỆT', style: AcText.label(size: 11, color: ac.white)),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// The central POWER key — round, accent-filled + glowing when on, dim when off.
class _PowerKey extends StatelessWidget {
  const _PowerKey({required this.on, required this.onTap});
  final bool on;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Material(
      color: Colors.transparent,
      shape: const CircleBorder(),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        customBorder: const CircleBorder(),
        child: Container(
          width: 76,
          height: 76,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            color: on ? ac.ice : ac.carbonUp,
            border: Border.all(color: on ? ac.ice : ac.carbonLine, width: 2),
            boxShadow: on ? [BoxShadow(color: ac.iceGlow, blurRadius: 16)] : null,
          ),
          child: Icon(Icons.power_settings_new, size: 32, color: on ? Colors.white : ac.whiteDim),
        ),
      ),
    );
  }
}
