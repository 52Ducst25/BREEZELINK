#include "led-matrix-logo.h"

#include <Arduino.h>
#include <Arduino_LED_Matrix.h>

namespace LedLogo {
namespace {

// Ma trận của UNO Q: 13 cột × 8 hàng, 3 bit xám (0..7).
constexpr uint8_t COLS = 13;
constexpr uint8_t ROWS = 8;
constexpr uint16_t CELLS = COLS * ROWS;

// --- Nhịp của vòng lặp --------------------------------------------------------
//
//   VẼ (5s)  ->  SÁNG (3s)  ->  XOÁ NGƯỢC (5s)  ->  quay lại   · trọn vòng 13s
//
// Pha SÁNG ở giữa cho mắt một nhịp nghỉ để đọc ra hình chữ Q trước khi nó bắt
// đầu tan — không có nó thì nét vừa khép kín đã tan ngay, và logo hoàn chỉnh
// chỉ tồn tại đúng một khung hình.
constexpr uint32_t DRAW_MS = 5000;
constexpr uint32_t HOLD_MS = 3000;
constexpr uint32_t ERASE_MS = 5000;

// 40ms/khung = 25 hình/giây, chỉ dùng cho hai pha ĐỘNG. Pha SÁNG đứng yên nên
// nó vẽ đúng một lần lúc bước vào — ma trận có mạch quét riêng chạy bằng ngắt,
// khung đã nạp thì nó tự giữ mà không tốn chu kỳ nào của vòng lặp.
//
// Phần lớn khung sẽ giống hệt khung trước — 40 ô sáng trải trên 255 mốc nghĩa
// là trung bình 125ms mới có một ô đổi trạng thái. Vẫn vẽ đủ 25 hình/giây vì
// phép dựng chỉ là 104 lần so sánh, rẻ hơn nhiều so với việc phải nhớ xem
// khung trước đã vẽ tới đâu.
constexpr uint32_t FRAME_MS = 40;

// Đặt 0 nếu muốn logo đứng yên, sáng liên tục.
#define LOGO_ANIMATE 1

// --- Bảng ---------------------------------------------------------------------
//
// Chữ Q của Qualcomm. Sinh bằng hàm khoảng cách (vành tròn + đoạn thẳng làm
// đuôi) rồi lượng tử hoá về 8 mức — xem hình ASCII bên phải.
//
// Các ô giá trị 2-5 KHÔNG PHẢI "mờ", mà là khử răng cưa: chúng nằm ở rìa nét và
// chính chúng làm một vòng tròn 8 điểm ảnh trông ra vòng tròn.
const uint8_t LOGO[CELLS] = {
    0, 0, 0, 0, 2, 5, 4, 0, 0, 0, 0, 0, 0,  //     :+=
    0, 0, 0, 6, 7, 5, 6, 7, 3, 0, 0, 0, 0,  //    *#+*#-
    0, 0, 3, 7, 0, 0, 0, 2, 7, 0, 0, 0, 0,  //   -#   :#
    0, 0, 7, 3, 0, 0, 0, 0, 7, 2, 0, 0, 0,  //   #-    #:
    0, 0, 7, 3, 0, 0, 4, 3, 7, 2, 0, 0, 0,  //   #-  =-#:
    0, 0, 3, 7, 0, 0, 1, 7, 7, 0, 0, 0, 0,  //   -#  .##
    0, 0, 0, 6, 7, 5, 6, 7, 7, 4, 0, 0, 0,  //    *#+*##=
    0, 0, 0, 0, 2, 5, 4, 0, 0, 7, 5, 0, 0,  //     :+=  #+
};

// Mốc xuất hiện của từng ô, thang 0..255. Ô nào có mốc ≤ tiến độ thì đã được vẽ.
//
// THỨ TỰ LÀ CÁCH NGƯỜI TA VIẾT TAY CHỮ Q: đi quanh vành trước, bắt đầu từ đỉnh
// theo chiều kim đồng hồ (mốc 0..200), rồi mới tới cái đuôi (200..255). Để đuôi
// hiện xen giữa lúc vành còn dở thì nó trông như nhiễu chứ không như nét vẽ.
//
// BẢNG TRA THAY VÌ TÍNH LƯỢNG GIÁC LÚC CHẠY: mỗi khung chỉ còn một phép so sánh
// cho mỗi ô, trên con vi điều khiển đang phải đọc UART không được sót byte nào.
//
// 255 ở ô tắt là vô hại — chúng bị chặn bởi phép kiểm LOGO[i] == 0 trước đó.
const uint8_t ORDER[CELLS] = {
    255, 255, 255, 255, 189, 197,   6, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 176, 185, 196,   9,  19,  26, 255, 255, 255, 255,
    255, 255, 164, 168, 255, 255, 255,  27,  34, 255, 255, 255, 255,
    255, 255, 155, 157, 255, 255, 255, 255,  44,  46, 255, 255, 255,
    255, 255, 145, 143, 255, 255, 200, 203,  56,  54, 255, 255, 255,
    255, 255, 136, 132, 255, 255, 202, 212,  66, 255, 255, 255, 255,
    255, 255, 255, 124, 115, 104,  91,  81, 230, 239, 255, 255, 255,
    255, 255, 255, 255, 111, 103,  94, 255, 255, 248, 255, 255, 255,
};

Arduino_LED_Matrix matrix;

uint8_t frame[CELLS];

enum Phase : uint8_t { PHASE_DRAW, PHASE_HOLD, PHASE_ERASE };
Phase phase = PHASE_DRAW;
uint32_t phaseStartMs = 0;
uint32_t lastFrameMs = 0;

/// Vẽ trạng thái dở dang của nét: chỉ những ô đã tới lượt.
///
/// XOÁ NGƯỢC DÙNG CHUNG ĐÚNG HÀM NÀY, chỉ khác là tiến độ chạy từ 255 về 0.
/// Nhờ vậy nét tan đi theo đúng đường nó đã mọc ra, chỉ ngược chiều — đuôi chữ
/// Q biến mất trước, rồi vành tháo dần ngược kim đồng hồ về lại đỉnh. Viết một
/// hàm xoá riêng thì gần như chắc chắn hai đường sẽ lệch nhau ở đâu đó.
void renderProgress(uint8_t progress) {
  for (uint16_t i = 0; i < CELLS; i++) {
    frame[i] = (LOGO[i] != 0 && ORDER[i] <= progress) ? LOGO[i] : 0;
  }
  matrix.draw(frame);
}

}  // namespace

void begin() {
  matrix.begin();
  // BẮT BUỘC trước draw(): mặc định thư viện hiểu đệm là thang 256 mức, nên
  // bảng 0..7 của ta sẽ hiện ra gần như tắt hẳn.
  matrix.setGrayscaleBits(3);

#if LOGO_ANIMATE
  phase = PHASE_DRAW;
  phaseStartMs = millis();
  renderProgress(0);        // bắt đầu từ màn hình trống, nét mọc dần ra
#else
  matrix.draw(LOGO);
#endif
}

void update() {
#if LOGO_ANIMATE
  const uint32_t now = millis();
  const uint32_t elapsed = now - phaseStartMs;

  switch (phase) {
    case PHASE_DRAW:
      if (elapsed >= DRAW_MS) {
        phase = PHASE_HOLD;
        phaseStartMs = now;
        matrix.draw(LOGO);          // khép kín nét, rồi đứng yên suốt pha SÁNG
        return;
      }
      if (now - lastFrameMs < FRAME_MS) return;
      lastFrameMs = now;
      // Nhân trước rồi mới chia để không mất độ phân giải. elapsed < 5000 nên
      // tích tối đa là 4999×255 ≈ 1,27 triệu — vừa uint32, không tràn.
      renderProgress((uint8_t)(elapsed * 255u / DRAW_MS));
      return;

    case PHASE_HOLD:
      // Đứng yên: không vẽ lại gì cả, chỉ chờ hết giờ.
      if (elapsed >= HOLD_MS) {
        phase = PHASE_ERASE;
        phaseStartMs = now;
        lastFrameMs = 0;
      }
      return;

    case PHASE_ERASE:
      if (elapsed >= ERASE_MS) {
        phase = PHASE_DRAW;
        phaseStartMs = now;
        lastFrameMs = 0;
        renderProgress(0);          // sạch hẳn rồi mới vẽ lại từ đầu
        return;
      }
      if (now - lastFrameMs < FRAME_MS) return;
      lastFrameMs = now;
      renderProgress((uint8_t)(255u - elapsed * 255u / ERASE_MS));
      return;
  }
#endif
}

}  // namespace LedLogo
