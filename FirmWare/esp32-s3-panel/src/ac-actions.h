#pragma once
#include <Arduino.h>

// ============================================================================
//  Các NÚT RỜI mà panel biết bắn — bảng dùng chung giữa loop() và giao diện.
// ----------------------------------------------------------------------------
//  "Nút rời" = một khung IR học được, phát lại nguyên văn, KHÔNG thuộc ma trận
//  (chế độ, nhiệt độ). Backend giữ chúng trong bảng `ir_action_codes`, khoá theo
//  (org, action) — xem src/app/services/ir_action_service.py.
//
//  VÌ SAO CÓ FILE NÀY, thay vì để mỗi bên tự khai:
//  tên trên dây ("FAN_60") là KHOÁ TRA trong NVS, còn nhãn tiếng Việt ("60%") là
//  thứ hiện trên màn. Hai bảng nằm ở hai file thì chỉ cần chèn thêm một mức vào
//  giữa ở một bên là mọi chỉ số lệch nhau — và hỏng theo kiểu tệ nhất có thể:
//  người dùng bấm "60%" và máy chạy 80%, không có lỗi nào ở đâu cả.
//
//  BẢNG NÀY PHẢI KHỚP `ir_action_service.KNOWN_ACTIONS`. Backend thêm nút mới thì
//  panel KHÔNG bắt buộc phải theo — nó chỉ bắn được những nút có mặt ở đây, và
//  các nút còn lại vẫn dùng bình thường trong app. Nói cách khác: file này là
//  TẬP CON panel hỗ trợ, không phải bản sao đầy đủ.
//
//  VÀ NÓ CỐ Ý LÀ TẬP CON. Mỗi khung IR chiếm ~600 byte NVS (~25 ô); chép hết 19
//  nút vào một bo không có nút bấm cho phần lớn trong số đó là trả phí flash cho
//  thứ không bao giờ phát được. SLEEP / ECO / SWING / TIMER... vẫn học và bắn
//  được từ app như cũ.
// ============================================================================
namespace AcActions {

/// Số mức quạt panel hiện được. 5 mức phần trăm + TỰ ĐỘNG + nút vòng của remote.
constexpr uint8_t FAN_COUNT = 7;

/// Tên trên dây, khớp `FAN_LEVELS` + `FAN_SPEED` của backend. Đây cũng là bí
/// danh trong IrStore (khoá NVS "a" + tên, dài nhất "aFAN_SPEED" = 10 ký tự,
/// dưới giới hạn 15 của NVS).
inline const char *fanWire(uint8_t i) {
  static const char *const k[FAN_COUNT] = {
      "FAN_20", "FAN_40", "FAN_60", "FAN_80", "FAN_100", "FAN_AUTO", "FAN_SPEED"};
  return i < FAN_COUNT ? k[i] : "";
}

/// Nhãn hiện trên màn. TIẾNG VIỆT như mọi nhãn khác của panel — màn ĐIỀU KHIỂN
/// đã gọi COOL là "LẠNH", nên hiện "FAN_60" ở ngay dưới đó là bắt người dùng học
/// hai hệ tên cho cùng một cái máy.
///
/// "NÚT VÒNG" nói đúng hành vi của FAN_SPEED: nó nhảy sang NẤC KẾ TIẾP theo vòng
/// của chính máy, nên panel KHÔNG biết bấm xong máy đang ở mức nào. Gọi nó là
/// một "mức" như sáu cái trên là nói dối.
inline const char *fanLabel(uint8_t i) {
  static const char *const k[FAN_COUNT] = {
      "20%", "40%", "60%", "80%", "100%", "TỰ ĐỘNG", "NÚT VÒNG"};
  return i < FAN_COUNT ? k[i] : "";
}

/// Máy tạo độ ẩm — hai ô mã, học từ app.
///
/// Hộ nào remote máy tạo ẩm chỉ có MỘT nút nguồn (bập bênh) thì học cùng một
/// khung vào cả hai ô, hoặc chỉ học ô BẬT — main.cpp tự nhận ra và dùng lại ô
/// BẬT cho chiều tắt. Xem humidifier-control.h.
inline const char *humidWire(bool on) { return on ? "HUMID_ON" : "HUMID_OFF"; }

}  // namespace AcActions
