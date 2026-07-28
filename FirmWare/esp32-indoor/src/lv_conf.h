// Guard PHẢI là `LV_CONF_H` chứ không phải `#pragma once`: lv_conf_internal.h
// kiểm tra đúng tên macro này để biết file cấu hình đã được nạp hay chưa. Dùng
// pragma once thì LVGL vẫn build nhưng in cảnh báo "Possible failure to include
// lv_conf.h" mỗi lần dịch — và ta sẽ không biết cấu hình có thật sự vào không.
#ifndef LV_CONF_H
#define LV_CONF_H

// ============================================================================
//  Cấu hình LVGL cho node TRONG NHÀ trên bo QR Box Advance.
// ----------------------------------------------------------------------------
//  CHỈ ghi đè những gì khác mặc định. `lv_conf_internal.h` bọc mọi thiết lập
//  trong `#ifndef` nên file này không cần chép nguyên mẫu 800 dòng — chép về là
//  mỗi lần nâng LVGL lại phải đi so từng dòng xem cái nào mới.
//
//  RÀNG BUỘC CHI PHỐI: ESP32-WROOM-32E-N8 **không có PSRAM**. Toàn bộ ngân sách
//  nằm trong ~320 KB DRAM, mà WiFi + MQTT đã ăn quá nửa. Mọi con số dưới đây là
//  hệ quả của việc đó, không phải chọn bừa.
// ============================================================================

// --- Màu ---------------------------------------------------------------------
#define LV_COLOR_DEPTH 16
// TFT_eSPI đẩy RGB565 qua SPI theo thứ tự byte ngược với LVGL. Không bật cờ này
// thì màn hiện đúng bố cục nhưng SAI MÀU (xanh ra cam) — lỗi trông như hỏng
// phần cứng nên rất dễ đi tìm nhầm chỗ.
#define LV_COLOR_16_SWAP 1

// --- Bộ nhớ ------------------------------------------------------------------
//  Bỏ mảng tĩnh, dùng heap của ESP-IDF.
//
//  GHI CHÚ ĐỂ KHỎI LẶP LẠI SAI LẦM: khi màn hình hiện thiếu nội dung, tôi đoán
//  ngay là hết heap LVGL và nâng LV_MEM_SIZE 28 -> 64 KB. Đoán SAI HOÀN TOÀN —
//  thủ phạm là font nén (xem mục LV_USE_FONT_COMPRESSED bên dưới). Đo thật thì
//  heap chưa bao giờ xuống dưới 170 KB tự do. Bài học: bật log rồi hãy sửa.
//
//  Dù vậy vẫn giữ LV_MEM_CUSTOM=1, vì trên đường đi có một phát hiện thật: đặt
//  LV_MEM_SIZE 64 KB thì ĐỨT ở bước liên kết —
//    "region `dram0_0_seg' overflowed by 17432 bytes"
//  Con số "327 KB RAM" PlatformIO in ra là TỔNG DRAM; riêng đoạn dữ liệu TĨNH
//  (.bss) của ESP32 bị chặn ở ~160 KB. Tức là mảng tĩnh gần như đã kịch trần,
//  và bản cũ chỉ còn ~10 KB dư — thêm một màn hình nữa là hỏng lúc dịch.
//
//  Đổi lại khi dùng malloc/free:
//    + không còn trần cứng — lấy đúng bằng lượng cần, chung hồ với WiFi/MQTT
//    + hết hẳn nhóm lỗi "vừa đủ lúc dịch, thiếu lúc chạy" vốn im lặng
//    - lv_mem_monitor() trả về 0 (nó chỉ đếm được bộ cấp phát riêng của LVGL),
//      nên ui.cpp theo dõi bằng ESP.getFreeHeap() — xem chú thích ở đó
//    - malloc chậm hơn bộ cấp phát TLSF của LVGL, nhưng giao diện này dựng một
//      lần lúc khởi động rồi chỉ đổi chữ, gần như không cấp phát lúc chạy
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

// --- Nhịp vẽ và nhịp đọc chạm ------------------------------------------------
//  Mặc định của LVGL là 30 ms cho cả hai. Hạ xuống vì hai thứ này quyết định
//  cảm giác "mượt" nhiều hơn hẳn tốc độ SPI:
//
//  ĐỌC CHẠM 30 -> 15 ms: đây là ĐỘ TRỄ TỪ LÚC NGÓN TAY CHẠM tới lúc nút đổi
//  màu. 30 ms nghe nhỏ nhưng cộng với 200 Hz của vòng lặp tác vụ và thời gian
//  đẩy SPI thì thành ~50-60 ms — đủ để tay cảm thấy nút "nặng". Chip cảm ứng
//  nói chuyện I2C 100 kHz, đọc dày hơn không tốn gì đáng kể.
//
//  VẼ 30 -> 20 ms: 50 khung/giây. Nhanh hơn nữa là vô ích — SPI 27 MHz đẩy hết
//  màn đã mất ~46 ms (76.800 điểm × 16 bit ÷ 27 MHz), nên đây không phải chỗ nghẽn; chỉ cần đủ để LVGL không
//  gộp nhiều thay đổi vào một khung rồi giật một nhịp.
#define LV_DISP_DEF_REFR_PERIOD  20
#define LV_INDEV_DEF_READ_PERIOD 15

// --- Nhịp thời gian ----------------------------------------------------------
//  Tự gọi lv_tick_inc() trong vòng lặp tác vụ UI thay vì móc vào millis():
//  tác vụ này ngủ bằng vTaskDelay nên biết chính xác đã trôi bao nhiêu ms.
#define LV_TICK_CUSTOM 0

// --- Font: TIẾNG VIỆT CÓ DẤU -------------------------------------------------
//  TẮT HẾT Montserrat dựng sẵn: chúng chỉ có ASCII + ° + •, nên "LÀM LẠNH" ra ô
//  vuông hoặc mất dấu. Bật cả bộ mà không dùng thì tốn ~150 KB flash cho code
//  chết. Font thật nằm ở src/ui/fonts/aircon_*.c, sinh từ Arial bằng
//  lv_font_conv (tools/make_lvgl_fonts.ps1) với dải 0x1EA0-0x1EF9.
//
//  LV_FONT_CUSTOM_DECLARE là cách LVGL cho phép LV_FONT_DEFAULT trỏ vào font
//  ngoài: lv_conf.h được nạp TRƯỚC mọi header của dự án nên không thể #include
//  theme.h ở đây.
#define LV_FONT_MONTSERRAT_14 0

// NGOẠI LỆ DUY NHẤT: bật Montserrat 20 KHÔNG PHẢI để hiện chữ, mà để lấy bộ
// BIỂU TƯỢNG. Font dựng sẵn của LVGL nhúng kèm các glyph LV_SYMBOL_* (dải
// 0xF000+, trích từ FontAwesome) — đó là cách duy nhất có icon mà không phải
// thêm file ảnh cho từng cái. Thanh điều hướng dùng chúng: nhà, nguồn, danh
// sách, bánh răng.
//
// Chữ tiếng Việt VẪN dùng aircon_viet_* — Montserrat không có dấu, đừng bao giờ
// đặt tiếng Việt vào font này.
#define LV_FONT_MONTSERRAT_20 1

#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(aircon_viet_16);
#define LV_FONT_DEFAULT &aircon_viet_16

// --- Cắt bớt thứ không dùng --------------------------------------------------
//  Giao diện chỉ dùng label/button/bar/bám lưới thủ công. Tắt phần còn lại để
//  khỏi tốn flash và RAM cho code không bao giờ chạy.
// BẬT LOG, mức WARN. Trước để 0 nên lần hết heap vừa rồi LVGL im như thóc và
// tôi mất công đoán. WARN gần như không in gì lúc chạy bình thường — chỉ lên
// tiếng khi cấp phát trượt, đúng lúc cần. LV_LOG_PRINTF đẩy thẳng ra printf,
// trên Arduino-ESP32 chính là UART0 (Serial), không phải bắc cầu gì thêm.
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

// Giao diện tự quản lý theme bằng style riêng (ui/theme.cpp) để bám đúng hệ
// thiết kế của web admin — theme mặc định của LVGL bo tròn góc, ngược hẳn.
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_BASIC 0

#endif // LV_CONF_H
