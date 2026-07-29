#pragma once
#include <Arduino.h>

// ============================================================================
//  Ngoại vi rời của bo QR Box Advance: đèn nền, còi, đồng hồ DS1307.
// ----------------------------------------------------------------------------
//  Gom vào một chỗ vì cả ba đều là ngoại vi rời của bo, không liên quan gì tới
//  logic điều hoà — tách ra thì ui.cpp chỉ còn phần vẽ và chạm.
//
//  DS1307 nằm chung bus I2C với cảm ứng, nên Touch::begin() phải chạy TRƯỚC
//  (nó là nơi gọi Wire.begin + ghim 100kHz).
// ============================================================================
namespace BoardIo {

// --- Đèn nền: Q5 BSS138 hạ áp phía mát, mức CAO = sáng ---
void backlightBegin(uint8_t pin);
/// 0..100. Sàn thực tế 10%: 0% làm màn nhìn y như hỏng, người dùng sẽ tưởng
/// node chết rồi đi rút điện.
void backlightSet(uint8_t percent);
uint8_t backlightGet();

// --- Còi MLT-8530 qua Q7 ---
void buzzerBegin(uint8_t pin);
void buzzerEnable(bool on);
bool buzzerEnabled();
/// Bíp KHÔNG CHẶN: đặt hẹn giờ rồi trả về ngay. buzzerTick() tắt hộ.
void beep(uint16_t ms = 40, uint16_t freq = 2700);
void buzzerTick();

// --- Cảm biến nhiệt/ẩm SHT3x (I2C 0x44) ---
// ---------------------------------------------------------------------------
//  THAY CHO DHT, không phải nâng cấp cho vui: trên bo này GPIO4 đã là I2C SCL,
//  và không còn GPIO trống nào cho một dây DHT (Interface/README.md §3.1). Bus
//  I2C thì đã sẵn trở kéo và đã chạy cho DS1307, thừa chỗ cho một địa chỉ nữa.
//
//  0x44 chọn có lý do: KHÔNG dùng AHT20 — nó ở 0x38, trùng chip cảm ứng FT6236.
//
//  CHỈ GỌI TỪ TÁC VỤ UI: bus I2C thuộc về tác vụ đó. loop() lấy số đo qua
//  Ui::readIndoor() chứ không tự đọc bus.
// ---------------------------------------------------------------------------
bool sht3xBegin();
bool sht3xPresent();
/// Đo một lần. Trả false nếu chip không trả lời hoặc CRC sai — KHÔNG bao giờ
/// trả số đoán. Chặn ~20ms (thời gian chuyển đổi của chip).
bool sht3xRead(float &tempC, float &humidity);

// --- DS1307 (I2C 0x68) ---
struct Clock { uint8_t hh, mm, ss; };

/// Đọc giờ. Trả false nếu chip không trả lời HOẶC đang ở trạng thái dừng dao
/// động (bit CH) — tức là chưa từng được đặt giờ. Không đoán bừa: giao diện
/// hiện "--:--" còn hơn hiện 00:00 như thể đó là giờ thật.
///
/// CHỈ CÒN CHIỀU ĐỌC. Node không đặt giờ nữa: nút "ĐỒNG BỘ GIỜ" cùng cặp
/// ntpBegin()/ntpPoll() và clockWrite() đã bỏ. Hệ quả phải biết trước: DS1307
/// phải được đặt giờ bằng công cụ khác (nó có pin nuôi riêng nên đặt một lần là
/// giữ). Chưa từng đặt thì thanh trạng thái hiện "--:--" vĩnh viễn — đúng theo
/// luật "thà không có số còn hơn số bịa", nhưng nhìn ra là đồng hồ hỏng.
bool clockRead(Clock &out);

} // namespace BoardIo
