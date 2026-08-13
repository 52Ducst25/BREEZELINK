#pragma once
#include <Arduino.h>

// ============================================================================
//  Cảm ứng điện dung I2C của module màn 2.8" (J1 trên bo QR Box Advance).
// ----------------------------------------------------------------------------
//  VÌ SAO TỰ DÒ CHIP: schematic chỉ vẽ tới đầu nối J1 (SCL/SDA/INT/RST) — chip
//  cảm ứng nằm trên module màn, KHÔNG có trong bản vẽ. Lô module khác nhau gắn
//  FT6236 / GT911 / CST816S tuỳ nguồn hàng, mà cả ba đều là I2C 4 dây y hệt.
//  Nạp cứng một loại thì đổi lô hàng là màn "chết cảm ứng" không rõ lý do; dò
//  theo địa chỉ lúc khởi động rẻ hơn nhiều so với việc đi tìm lỗi đó.
//
//  BUS DÙNG CHUNG VỚI DS1307 (0x68) — xem board-io.h. DS1307 chỉ chịu 100kHz
//  nên cả bus bị ghim ở 100kHz; GT911 chạy chậm hơn nhưng vẫn đúng.
// ============================================================================
namespace Touch {

enum Chip : uint8_t { NONE = 0, FT6236, GT911, CST816 };

/// Khởi tạo Wire (nếu chưa) + nhả RST + dò địa chỉ. Trả false nếu không thấy
/// chip nào — giao diện vẫn vẽ bình thường, chỉ là không bấm được.
bool begin(uint8_t sdaPin, uint8_t sclPin, uint8_t rstPin, uint8_t intPin);

Chip chip();
const char *chipName();

/// Toạ độ ĐÃ xoay về hệ 320x240 nằm ngang của TFT.
/// Trả true khi đang có ngón chạm. Không chặn, không delay.
bool read(int16_t &x, int16_t &y);

} // namespace Touch
