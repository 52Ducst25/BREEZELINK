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
  // Các mức quạt ĐẶT THẲNG — mỗi mức một mã IR học riêng. Xem FAN_LEVELS trong
  // services/ir_action_service.py để biết vì sao không tái dùng FAN_SPEED.
  AcAction('FAN_20', 'Quạt 20%', Icons.air),
  AcAction('FAN_40', 'Quạt 40%', Icons.air),
  AcAction('FAN_60', 'Quạt 60%', Icons.air),
  AcAction('FAN_80', 'Quạt 80%', Icons.air),
  AcAction('FAN_100', 'Quạt 100%', Icons.air),
  AcAction('FAN_AUTO', 'Quạt tự động', Icons.auto_mode),
  // Nút "quạt" kiểu vòng của remote. GIỮ LẠI cho org đã học từ trước — bỏ đi là
  // mã họ đã học thành mồ côi, bấm vào báo lỗi mà không ai hiểu vì sao.
  AcAction('FAN_SPEED', 'Quạt (nút vòng)', Icons.loop),
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
  // Máy tạo độ ẩm — KHÔNG phải nút của remote điều hoà, mà của một máy khác
  // trong cùng phòng. Nằm chung danh sách này vì cơ chế y hệt: một khung IR học
  // được, phát lại nguyên văn.
  //
  // ĐIỀU PHẢI BIẾT KHI BẤM HAI NÚT NÀY TRONG APP: chúng chỉ bắn một phát ngay
  // lúc bấm. Việc lái máy tạo ẩm theo độ ẩm phòng do PANEL tự làm, liên tục, kể
  // cả khi mất mạng — nên bấm ở đây là "bật/tắt ngay bây giờ", còn panel sẽ
  // quyết định lại ở lần kiểm kế tiếp. Muốn giữ nguyên ý mình thì bấm GHI ĐÈ
  // trên panel.
  AcAction('HUMID_ON', 'Tạo ẩm: bật', Icons.water_drop),
  AcAction('HUMID_OFF', 'Tạo ẩm: tắt', Icons.water_drop_outlined),
];
