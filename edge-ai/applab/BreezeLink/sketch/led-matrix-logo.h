/*
  Logo Qualcomm động trên ma trận LED 13x8 của UNO Q.

  Vòng lặp ba pha, tổng 13 giây:

      VẼ 5s            nét mọc dần từ đỉnh, quanh vành theo chiều kim đồng hồ,
                       đuôi chữ Q hiện cuối cùng — đúng cách viết tay
      SÁNG 3s          đứng yên, sáng đủ — cho mắt kịp đọc ra hình chữ Q
      XOÁ NGƯỢC 5s     nét tan theo đúng đường đó nhưng ngược chiều: đuôi mất
                       trước, vành tháo dần ngược kim đồng hồ về lại đỉnh

  TÁCH RA KHỎI sketch.ino vì nó không liên quan gì tới đường dữ liệu: sketch
  chính lo UART và RPC, còn đây chỉ là hiển thị. Trộn vào một file thì mỗi lần
  gỡ lỗi giao thức lại phải cuộn qua hai bảng 104 số.
*/

#pragma once

#include <stdint.h>

namespace LedLogo {

/// Bật ma trận và bắt đầu vòng lặp từ màn hình trống. Gọi một lần trong setup().
void begin();

/// Đẩy vòng lặp đi tiếp. Gọi mỗi vòng loop() — hàm tự đếm thời gian, và chỉ
/// pha VẼ mới thật sự dựng hình (25 khung/giây); hai pha kia vẽ đúng một lần
/// lúc bước vào rồi để mạch quét của ma trận tự giữ.
///
/// GỌI SAU pump(): đệm UART của Zephyr có hạn, và một khung 39 byte tới trong
/// lúc đang bận dựng hình là một khung mất.
void update();

}  // namespace LedLogo
