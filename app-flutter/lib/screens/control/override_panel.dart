import 'dart:async';

import 'package:flutter/material.dart';

import '../../models/ac_action.dart';
import '../../models/ac_mode.dart';
import '../../models/ac_state.dart';
import '../../models/config_bounds.dart';
import '../../theme/ac_colors.dart';
import '../../theme/ac_shapes.dart';
import '../../theme/ac_text.dart';
import '../../widgets/icon_badge.dart';
import 'fan_speed_sheet.dart';
import 'temp_dial.dart';

/// The AC remote, restyled to the BenKon look: a big circular temperature dial,
/// a prominent power control, "Chế độ" + "Tốc độ quạt" cards, and a tidy grid
/// of the peripheral learned actions.
///
/// Two command paths (UNCHANGED behaviour):
///  - POWER / TEMP / MODE change the operating (mode, setpoint) via [onSubmit]
///    (a manual override) — these always work.
///  - Every other key replays ONE learned IR frame via [onAction]. If that
///    button hasn't been learned yet the server says so and the status line
///    tells the user to go learn it — no key is ever silently dead.
class OverridePanel extends StatefulWidget {
  const OverridePanel({
    super.key,
    required this.bounds,
    required this.onSubmit,
    required this.onAction,
    this.acState,
  });

  final ConfigBounds? bounds;
  final Future<String?> Function(AcMode mode, int setpoint) onSubmit;
  final Future<String?> Function(String action) onAction;

  /// Máy lạnh ĐANG được đặt ở mức nào, theo máy chủ. null = chưa có lệnh nào
  /// từng chạy, khi đó dial dùng mặc định ở [_kFallbackSetpoint].
  ///
  /// THAY CHO `initialMode`/`initialSetpoint` CŨ, và đó là một lỗi thật chứ không
  /// phải dọn dẹp cho gọn. Hai tham số kia (a) không được ControlScreen truyền
  /// bao giờ nên luôn rơi về COOL/25 ghi cứng, và (b) kể cả có truyền thì
  /// `late _mode = widget.initialMode` chỉ chạy MỘT LẦN — Dart không tính lại bộ
  /// khởi tạo field khi widget cha dựng lại. Nên dial của app là một giá trị cục
  /// bộ không bao giờ đọc máy chủ: panel đặt 26 độ thì app vẫn khoe 25.
  /// Tên mới là `acState` để không ai còn đọc nó là "giá trị ban đầu".
  final AcState? acState;

  @override
  State<OverridePanel> createState() => _OverridePanelState();
}

enum _SendState { idle, sending, sent, error }

const _kModes = [AcMode.cool, AcMode.dry, AcMode.fan];

/// Mọi nút thuộc về thẻ "Tốc độ quạt" — không hiện trong lưới nút phụ.
const _kFanWires = {'FAN_20', 'FAN_40', 'FAN_60', 'FAN_80', 'FAN_100', 'FAN_AUTO', 'FAN_SPEED'};

/// Peripheral keys shown in the grid — every [kAcActions] entry EXCEPT the fan
/// ones, which have their own dedicated "Tốc độ quạt" card above.
final _kGridActions = kAcActions.where((a) => !_kFanWires.contains(a.wire)).toList();
AcAction _act(String wire) => kAcActions.firstWhere((a) => a.wire == wire);

/// Dial dùng mức này khi máy chủ chưa biết máy lạnh đang ở đâu. Chỉ là chỗ đặt
/// con trỏ ban đầu, KHÔNG phải trạng thái được khẳng định — và nó chỉ tồn tại tới
/// ảnh chụp đầu tiên có `ac`.
const _kFallbackSetpoint = 25;

/// Bao lâu sau cú chạm cuối thì mới cho máy chủ ghi lại giá trị trên dial.
///
/// Cùng lý do và cùng con số với `PEND_EDIT_LOCK_MS` trong firmware
/// (ui-screens.cpp): thiếu nó thì đúng lúc người dùng đang kéo dial mà một ảnh
/// chụp tới là con số giật về giá trị máy chủ ngay dưới ngón tay. Ở app còn tệ hơn
/// panel: `_step` tự gửi sau 500ms, nên một cú giật giữa chừng sẽ GỬI ĐI mức mà
/// người dùng không hề chọn.
const _kEditLock = Duration(seconds: 5);

class _OverridePanelState extends State<OverridePanel> {
  late AcMode _mode = widget.acState?.mode ?? AcMode.cool;
  late int _setpoint = widget.acState?.setpoint ?? _kFallbackSetpoint;
  late AcMode _lastActiveMode = _mode == AcMode.off ? AcMode.cool : _mode;
  _SendState _state = _SendState.idle;
  String? _statusMsg;
  Timer? _tempDebounce;
  Timer? _sentReset;

  /// Lần cuối người dùng chạm vào POWER / ± / chế độ. null = chưa chạm lần nào.
  DateTime? _touchedAt;

  bool get _on => _mode != AcMode.off;
  int get _min => widget.bounds?.clampMin.round() ?? 16;
  int get _max => widget.bounds?.clampMax.round() ?? 32;

  bool get _editing =>
      _touchedAt != null && DateTime.now().difference(_touchedAt!) < _kEditLock;

  /// Kéo dial theo trạng thái thật mỗi khi ảnh chụp mới tới.
  ///
  /// PHẢI Ở ĐÂY, không phải ở bộ khởi tạo field: Dart chỉ chạy `late ... =` một
  /// lần cho cả đời State, nên nếu chỉ dựa vào nó thì dial đóng băng ở ảnh chụp
  /// đầu tiên và mọi thay đổi từ panel/máy chủ về sau bị bỏ qua.
  @override
  void didUpdateWidget(OverridePanel old) {
    super.didUpdateWidget(old);
    final ac = widget.acState;
    if (ac == null || ac == old.acState) return;   // không có tin mới -> không sờ vào dial
    if (_editing) return;                          // người dùng đang chỉnh -> họ thắng
    if (_tempDebounce?.isActive ?? false) return;  // có lệnh đang chờ gửi -> đừng ghi lên nó
    setState(() {
      _mode = ac.mode;
      _setpoint = ac.setpoint;
      if (ac.mode != AcMode.off) _lastActiveMode = ac.mode;
    });
  }

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
    _touchedAt = DateTime.now();
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
    _touchedAt = DateTime.now();
    setState(() {
      _mode = _on ? _kModes[(_kModes.indexOf(_mode) + 1) % _kModes.length] : AcMode.cool;
      _lastActiveMode = _mode;
    });
    _tempDebounce?.cancel();
    _sendOverride();
  }

  void _step(int delta) {
    if (!_on) return;
    _touchedAt = DateTime.now();
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

  /// Mức quạt vừa gửi TỪ APP NÀY. null khi phiên này chưa gửi lệnh quạt nào.
  ///
  /// KHÔNG PHẢI trạng thái thật của máy, và không được trình bày như vậy. Lệnh
  /// quạt là một khung hồng ngoại bắn đi một chiều — máy chủ không lưu, `AcState`
  /// cũng không mang trường nào cho nó. Ai cầm remote thật bấm một cái là con số
  /// ở đây sai ngay mà app không có cách nào biết. Cùng nguyên tắc đã ghi trong
  /// models/ac_state.dart: thà nói "chưa rõ" còn hơn khẳng định một mức bịa.
  String? _fanWire;

  String _fanLabel() {
    final w = _fanWire;
    if (w == null) return 'Chọn mức';
    return kFanChoices.firstWhere((c) => c.$1 == w).$2;
  }

  Future<void> _pickFan() async {
    final picked = await showFanSpeedSheet(context, _fanWire);
    if (picked == null || !mounted) return;

    setState(() => _fanWire = picked);
    _showSending();
    final err = await widget.onAction(picked);
    // Máy chủ báo chưa học mức này -> nhả nhãn về "Chọn mức". Giữ nguyên nhãn
    // vừa bấm sẽ là nói dối: màn hình khoe 60% trong khi không có mã nào được
    // phát đi và máy vẫn chạy y như cũ.
    if (err != null && mounted) setState(() => _fanWire = null);
    _showResult(err, okMsg: 'Đã đặt quạt ${_fanLabel()}');
  }

  @override
  Widget build(BuildContext context) {
    final steppable = _on && _mode == AcMode.cool;
    return Column(
      children: [
        TempDial(
          mode: _mode,
          setpoint: _setpoint,
          on: _on,
          onDecrement: steppable ? () => _step(-1) : null,
          onIncrement: steppable ? () => _step(1) : null,
        ),
        const SizedBox(height: 20),
        _PowerBar(on: _on, onTap: _togglePower),
        const SizedBox(height: 16),
        Row(
          children: [
            Expanded(
              child: _ControlCard(
                icon: _mode.icon,
                title: 'Chế độ',
                value: _on ? _mode.label : 'TẮT',
                onTap: _cycleMode,
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _ControlCard(
                icon: Icons.air,
                title: 'Tốc độ quạt',
                value: _fanLabel(),
                onTap: _pickFan,
              ),
            ),
          ],
        ),
        const SizedBox(height: 16),
        GridView.count(
          crossAxisCount: 4,
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          mainAxisSpacing: 10,
          crossAxisSpacing: 10,
          childAspectRatio: 0.86,
          children: [
            for (final a in _kGridActions) _ActionKey(action: a, onTap: () => _action(a.wire)),
          ],
        ),
        const SizedBox(height: 16),
        _status(context),
      ],
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

/// Prominent full-width power control — accent-filled + glowing when on, dim
/// when off.
class _PowerBar extends StatelessWidget {
  const _PowerBar({required this.on, required this.onTap});
  final bool on;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Material(
      color: Colors.transparent,
      borderRadius: cardRadius,
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        child: Container(
          width: double.infinity,
          padding: const EdgeInsets.symmetric(vertical: 15),
          decoration: BoxDecoration(
            color: on ? ac.ice : ac.carbonUp,
            borderRadius: cardRadius,
            border: Border.all(color: on ? ac.ice : ac.carbonLine, width: 1.5),
            boxShadow: on ? [BoxShadow(color: ac.iceGlow, blurRadius: 18, spreadRadius: -2)] : null,
          ),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(Icons.power_settings_new, size: 20, color: on ? Colors.white : ac.whiteDim),
              const SizedBox(width: 10),
              Text(
                on ? 'ĐANG BẬT · NHẤN ĐỂ TẮT' : 'ĐÃ TẮT · NHẤN ĐỂ BẬT',
                style: AcText.label(size: 12, color: on ? Colors.white : ac.whiteDim),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// A rounded "setting" card ("Chế độ", "Tốc độ quạt") — leading icon badge,
/// title over a value, whole card tappable.
class _ControlCard extends StatelessWidget {
  const _ControlCard({required this.icon, required this.title, required this.value, required this.onTap});
  final IconData icon;
  final String title;
  final String value;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Material(
      color: Colors.transparent,
      borderRadius: cardRadius,
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.all(14),
          decoration: BoxDecoration(
            color: ac.carbonUp,
            borderRadius: cardRadius,
            border: Border.all(color: ac.carbonLine, width: 1),
          ),
          child: Row(
            children: [
              AcIconBadge(icon: icon, size: 40, iconSize: 20),
              const SizedBox(width: 10),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(title, style: AcText.label(size: 10.5, color: ac.whiteDim)),
                    const SizedBox(height: 3),
                    Text(value, maxLines: 1, overflow: TextOverflow.ellipsis, style: AcText.heading(size: 13, color: ac.white)),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// One peripheral remote key in the grid: an icon badge over a short label.
class _ActionKey extends StatelessWidget {
  const _ActionKey({required this.action, required this.onTap});
  final AcAction action;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final ac = context.ac;
    return Material(
      color: Colors.transparent,
      borderRadius: innerRadius,
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 4),
          decoration: BoxDecoration(
            color: ac.carbonUp,
            borderRadius: innerRadius,
            border: Border.all(color: ac.carbonLine, width: 1),
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(action.icon, size: 22, color: ac.white),
              const SizedBox(height: 6),
              Text(action.label,
                  textAlign: TextAlign.center, maxLines: 2, overflow: TextOverflow.ellipsis,
                  style: AcText.label(size: 9, color: ac.whiteDim)),
            ],
          ),
        ),
      ),
    );
  }
}
