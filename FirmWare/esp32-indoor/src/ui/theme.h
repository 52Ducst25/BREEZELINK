#pragma once
#include <TFT_eSPI.h>

// ============================================================================
//  Bảng màu + lưới bố cục cho màn 2.8" ILI9341 320x240 của bo QR Box Advance.
// ----------------------------------------------------------------------------
//  Màu quy đổi thẳng từ app Flutter (app-flutter/lib/theme/ac_colors.dart) sang
//  RGB565. Người dùng nhìn màn trên tường rồi mở app trong cùng một phút; hai
//  thứ lệch bảng màu bị đọc là hai sản phẩm khác nhau.
//
//  Hình học "Titanium Command": viền 1px, GÓC VÁT 45 độ (không bo tròn) — dùng
//  chamferRect() bên dưới chứ không dùng drawRoundRect() như bản phác Lopaka.
//
//  Toạ độ trong file này là HỢP ĐỒNG với ../../Interface/README.md §5: sửa số ở
//  đây thì sửa cả wireframe bên đó, nếu không lần sau đọc tài liệu sẽ ra sai.
// ============================================================================
namespace Theme {

// --- Nền / viền ---
static const uint16_t carbon          = 0x0862;  // #0A0E14 nền toàn màn
static const uint16_t carbonUp        = 0x10C4;  // #121924 nút bị vô hiệu
static const uint16_t carbonPanel     = 0x10E5;  // #141C28 thẻ / panel
static const uint16_t carbonLine      = 0x29C9;  // #2A3B4C viền thường
static const uint16_t carbonLineHi    = 0x3AAD;  // #3E5468 viền nổi

// --- Nhấn (xanh kỹ thuật của thương hiệu) ---
static const uint16_t ice             = 0x02BF;  // #0055FF nền nút đang chọn
static const uint16_t iceText         = 0x4C7F;  // #4D8DFF chữ nhấn trên nền tối

// --- Chữ / trạng thái ---
static const uint16_t white           = 0xE79F;  // #E7F1F8
static const uint16_t whiteDim        = 0x8D16;  // #8DA2B5
static const uint16_t success         = 0x262B;  // #22C55E
static const uint16_t error           = 0xFA69;  // #FF4D4D
static const uint16_t warning         = 0xF524;  // #F5A623

// --- Thang nhiệt: áp theo GIÁ TRỊ, không bao giờ theo "cảm biến nào" ---
static const uint16_t thermalCold     = 0x3D1F;  // #3AA0FF dưới vùng dễ chịu
static const uint16_t thermalNeutral  = 0x262B;  // #22C55E trong vùng dễ chịu
static const uint16_t thermalWarm     = 0xF524;  // #F5A623 trên vùng dễ chịu
static const uint16_t thermalHot      = 0xFA69;  // #FF4D4D nóng hẳn

/// Màu của một số đo nhiệt độ. Ngưỡng lấy theo dải ASHRAE 55 mà backend đang
/// dùng cho vùng "dễ chịu" — cùng ngôn ngữ màu với thẻ trên app.
uint16_t thermalColor(float celsius);

// --- Lưới ---
static const int16_t SCREEN_W   = 320;
static const int16_t SCREEN_H   = 240;
static const int16_t PAD        = 6;
static const int16_t STATUS_H   = 22;    // y 0..21, gạch phân cách ở y=21
static const int16_t CONTENT_Y  = 24;
static const int16_t CONTENT_H  = 180;   // y 24..203
static const int16_t NAV_Y      = 206;   // y 206..239
static const int16_t NAV_H      = 34;
static const int16_t NAV_W      = 80;    // 4 tab x 80 = 320
static const int16_t CHAMFER    = 6;     // độ vát góc

/// Ô chữ nhật + hành động chạm gắn với nó.
struct Rect {
  int16_t x, y, w, h;
  bool contains(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// --- TRANG CHU ---
static const Rect R_CARD_IN  = {  6,  26, 150, 96};
static const Rect R_CARD_OUT = {164,  26, 150, 96};
static const Rect R_CARD_AC  = {  6, 128, 308, 74};

// --- DIEU KHIEN ---
static const Rect R_MINUS    = {  8,  28,  68, 76};
static const Rect R_SETBOX   = { 84,  28, 152, 76};
static const Rect R_PLUS     = {244,  28,  68, 76};
static const Rect R_SEND     = {  6, 160, 150, 42};
static const Rect R_AUTO     = {164, 160, 150, 42};
static const int16_t MODE_Y  = 110, MODE_H = 44, MODE_W = 74, MODE_STEP = 78;
Rect modeRect(uint8_t i);   // i = 0..3  (LANH / KHO / QUAT / TAT)

// --- CAI DAT: 4 hàng đầy chiều rộng ---
Rect settingRow(uint8_t i); // i = 0..3
static const int16_t SET_Y0 = 28, SET_H = 40, SET_STEP = 46;

// --- HOC REMOTE (lớp phủ) ---
static const Rect R_LEARN    = { 16,  30, 288, 168};

/// Chữ nhật vát góc trên-trái và dưới-phải 45 độ — hình học thương hiệu.
/// [fill] = TFT_TRANSPARENT thì chỉ vẽ viền.
void chamferRect(TFT_eSPI &g, const Rect &r, uint16_t fill, uint16_t border,
                 int16_t cut = CHAMFER);

/// Panel chuẩn: nền carbonPanel + viền carbonLine.
void panel(TFT_eSPI &g, const Rect &r);

/// Nút: [active] tô nền ice, [enabled]=false tô carbonUp + chữ mờ.
void button(TFT_eSPI &g, const Rect &r, const char *label,
            bool active, bool enabled = true);

/// Huy hiệu nhỏ (TU DONG / GHI DE / CHUA HOC...).
void badge(TFT_eSPI &g, int16_t x, int16_t y, const char *text, uint16_t color);

} // namespace Theme
