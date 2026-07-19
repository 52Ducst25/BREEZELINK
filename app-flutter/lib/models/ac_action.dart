import 'package:flutter/material.dart';

/// One standalone remote button that is NOT part of the (mode, temp) matrix —
/// each is a single learned IR frame the app replays. [wire] MUST match a label
/// in the backend's `ir_action_service.KNOWN_ACTIONS`; [label]/[icon] are the
/// Vietnamese name + glyph shown on the remote key and in the learn list.
class AcAction {
  const AcAction(this.wire, this.label, this.icon);
  final String wire;
  final String label;
  final IconData icon;
}

/// The peripheral remote buttons, in the order they appear on the physical
/// remote. Kept in ONE place so the remote tab and the learn tab stay in sync —
/// add a button here (and to KNOWN_ACTIONS on the server) and both pick it up.
const kAcActions = <AcAction>[
  AcAction('FAN_SPEED', 'Tốc độ quạt', Icons.air),
  AcAction('SUPER', 'Siêu tốc', Icons.bolt),
  AcAction('SLEEP', 'Ngủ', Icons.bedtime_outlined),
  AcAction('ECO', 'Tiết kiệm', Icons.eco_outlined),
  AcAction('QUIET', 'Yên tĩnh', Icons.volume_off_outlined),
  AcAction('SMART', 'Thông minh', Icons.auto_awesome_outlined),
  AcAction('TIMER', 'Hẹn giờ', Icons.timer_outlined),
  AcAction('SWING_V', 'Đảo dọc', Icons.swap_vert),
  AcAction('SWING_H', 'Đảo ngang', Icons.swap_horiz),
  AcAction('LIGHT', 'Đèn', Icons.lightbulb_outline),
  AcAction('TEMP_UNIT', '°C/°F', Icons.thermostat_outlined),
];
