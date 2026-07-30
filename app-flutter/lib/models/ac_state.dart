import 'ac_mode.dart';
import 'parse_utils.dart';

/// Trạng thái THẬT của máy lạnh lúc này — ai đặt cũng vậy: vòng lặp comfort,
/// app này, hay người đứng bấm ở panel treo tường.
///
/// ĐỪNG LẪN VỚI `ComfortPreview.tSet`. Hai thứ trả lời hai câu hỏi khác nhau:
///   ComfortPreview.tSet  thuật toán SẼ chọn mức nào (backend cố tình tính với
///                        override_active=false, nên nó vẫn hiện khuyến nghị tự
///                        động ngay cả khi ghi đè thủ công đang thắng)
///   AcState.setpoint     máy ĐANG được đặt ở mức nào
///
/// Không có lớp này thì dial trong app không có nguồn nào để đọc trạng thái
/// thật, và nó rơi về COOL/25 ghi cứng — lệch với panel ngay khi ai đó chạm vào
/// một trong hai mặt. Server gửi null (chưa có lệnh nào từng chạy) thì để null,
/// TUYỆT ĐỐI không bịa ra một mức mặc định: một con số bịa trên dial điều khiển
/// máy lạnh sẽ được người dùng đọc là trạng thái thật.
class AcState {
  const AcState({required this.mode, required this.setpoint});

  final AcMode mode;
  final int setpoint;

  static AcState? fromJsonOrNull(dynamic json) {
    if (json is! Map) return null;
    final mode = acModeFromWire(json['mode'] as String?);
    final setpoint = asIntOrNull(json['setpoint']);
    if (mode == null || setpoint == null) return null;
    return AcState(mode: mode, setpoint: setpoint);
  }

  @override
  bool operator ==(Object other) =>
      other is AcState && other.mode == mode && other.setpoint == setpoint;

  @override
  int get hashCode => Object.hash(mode, setpoint);
}
