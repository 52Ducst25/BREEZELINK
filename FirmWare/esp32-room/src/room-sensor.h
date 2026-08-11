#pragma once
#include <Arduino.h>

// ============================================================================
//  Cảm biến nhiệt/ẩm của node góc phòng (DHT22).
// ----------------------------------------------------------------------------
//  Tách khỏi main.cpp không phải để bọc thư viện DHT, mà để giữ MỘT luật ở một
//  chỗ: "đọc trượt một lần" và "cảm biến đã hỏng" là hai chuyện khác nhau.
//
//  DHT22 trượt vài phần trăm số lần đọc là BÌNH THƯỜNG (sai checksum, hết giờ
//  chờ). Báo NaN ra ngoài ngay lần trượt đầu là node phát một gói "không có số
//  đo", gateway rút nó khỏi phép trung vị, rồi 2.5 giây sau lại có số — phòng
//  thấy nhiệt độ nhảy vì một lỗi đọc chẳng liên quan gì tới nhiệt độ.
//
//  Nên: giữ số đo hợp lệ gần nhất qua vài lần trượt, chỉ khi trượt LIÊN TIẾP đủ
//  nhiều mới thừa nhận là cảm biến hỏng và trả NaN. Lúc đó NaN là sự thật, và
//  gateway loại node này khỏi trung vị là đúng.
// ============================================================================
namespace RoomSensor {

/// Nhịp đọc (ms). Datasheet DHT22 ghi TỐI THIỂU 2s giữa hai lượt — dưới mức đó
/// chip trả lại số của lần trước chứ không đo mới. 2.5s cho dư biên.
static const unsigned long READ_PERIOD_MS = 2500;

/// Số lần trượt LIÊN TIẾP trước khi thừa nhận cảm biến hỏng. 6 × 2.5s = 15s,
/// đúng một nhịp gửi — đủ để một chuỗi nhiễu qua đi, chưa đủ để che một sợi dây
/// vừa tuột.
static const uint8_t FAIL_LIMIT = 6;

/// Dựng cảm biến trên [pin].
void begin(uint8_t pin);

/// Gọi mỗi vòng loop(); tự bỏ qua nếu chưa tới nhịp. Trả true nếu vừa đọc xong
/// một lượt (dù thành công hay trượt) — dùng để biết khi nào nên cập nhật gói.
bool poll();

/// Số đo hiện hành. Trả false (và để nguyên tham số) khi cảm biến đang bị coi
/// là hỏng hoặc chưa từng đọc được lần nào. KHÔNG bao giờ trả 0.0 thay cho
/// "không đo được".
bool read(float &tempC, float &humidity);

/// Số lần trượt liên tiếp hiện tại — để in ra log lúc truy lỗi lắp đặt.
uint8_t consecutiveFailures();

} // namespace RoomSensor
