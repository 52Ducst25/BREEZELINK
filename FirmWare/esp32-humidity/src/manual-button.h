#pragma once
#include <Arduino.h>

// ============================================================================
//  Nút BOOT có sẵn trên bo — hai cử chỉ, không cần hàn thêm gì.
// ----------------------------------------------------------------------------
//      Nhấn NHẢ NHANH   -> bật/tắt tay (vào GHI ĐÈ)
//      Giữ >= 3 giây    -> vào chế độ HỌC mã IR
//
//  VÌ SAO PHÁT SỰ KIỆN GIỮ NGAY KHI ĐỦ 3 GIÂY, chứ không đợi nhả tay: bo này
//  không có màn, nên thứ duy nhất báo "đủ lâu rồi, thả ra được" là LED đổi
//  kiểu nháy. Đợi nhả tay thì người dùng không biết bao giờ là đủ và sẽ giữ
//  thêm cho chắc, hoặc thả sớm và nó thành nhấn nhanh — tức là bật máy trong
//  khi họ định học mã.
//
//  Nhả tay sau đó bị NUỐT (không sinh thêm nhấn nhanh).
//
//  GIỮ NÚT LÚC RESET là vào bootloader nạp firmware — đó là chức năng gốc của
//  chân này và ta không đụng tới. Vô hại vì begin() chỉ chạy sau khi bo đã
//  khởi động xong; lúc ấy ROM không còn nhìn chân này nữa.
// ============================================================================
namespace ManualButton {

enum class Event : uint8_t { NONE, SHORT_PRESS, LONG_PRESS };

/// Bỏ rung phím (ms). Nút cơ nảy vài ms mỗi lần chạm; không lọc thì một lần
/// bấm sinh ra vài sự kiện, và với nút bật/tắt thì số lẻ hay chẵn quyết định
/// máy cuối cùng bật hay tắt — hỏng theo kiểu "lúc được lúc không".
static const uint32_t DEBOUNCE_MS = 40;

void begin(uint8_t pin);

/// Gọi mỗi vòng loop(). Trả sự kiện vừa xảy ra (nếu có).
Event poll();

}  // namespace ManualButton
