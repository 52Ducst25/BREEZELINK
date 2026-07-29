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
bool clockRead(Clock &out);

/// Đặt giờ cho DS1307 (chế độ 24h, xoá bit CH để dao động chạy).
///
/// PHẢI gọi từ tác vụ UI — DS1307 nằm trên bus I2C mà tác vụ đó sở hữu.
bool clockWrite(uint8_t hh, uint8_t mm, uint8_t ss);

// --- Đồng bộ giờ qua NTP ---
//
//  ĐÃ QUAY LẠI, NHƯNG TỰ ĐỘNG. Bản trước có nút "ĐỒNG BỘ GIỜ" trong Cài đặt và
//  nó bị gỡ ở 42332bc vì bắt người ta bấm tay là một việc vặt ai cũng quên. Gỡ
//  cả đường đặt giờ thì lộ ra hệ quả tệ hơn: DS1307 có pin nuôi riêng nên một
//  giá trị SAI cũng được giữ nguyên vĩnh viễn, mà bit CH đã xoá nên clockRead()
//  vẫn báo hợp lệ — màn hình hiện một giờ sai một cách tự tin, và không có bất
//  kỳ đường nào trong firmware sửa được nó.
//
//  Nên lần này không có nút: node tự đồng bộ khi có mạng, DS1307 lùi về đúng vai
//  trò của nó là giữ giờ lúc mất mạng/mất điện.
void ntpBegin();

/// Hỏi đồng hồ hệ thống; nếu SNTP đã về thì ghi xuống DS1307.
/// Trả false khi SNTP chưa có kết quả — KHÔNG chờ, gọi lại ở nhịp sau.
/// Cũng phải gọi từ tác vụ UI (nó ghi I2C).
bool ntpPoll();

} // namespace BoardIo
