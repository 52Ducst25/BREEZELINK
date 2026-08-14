#include "pin-diagnostics.h"

#include "settings.h"

namespace PinDiagnostics {
namespace {

/// Số lần lấy mẫu và nhịp lấy — 400 mẫu × 0,5ms = 200ms, đủ dài để một khung
/// IR lọt vào nếu có ai đang bấm remote, mà vẫn đủ ngắn để không chặn lâu.
const uint16_t SAMPLES = 400;
const uint16_t SAMPLE_GAP_US = 500;

}  // namespace

void checkIrReceiver() {
  Serial.printf("\n=== DO CHAN MAT THU (GPIO%d) ===\n", IR_RX_PIN);

  // Kéo XUỐNG rồi đọc: xem có ai chủ động lái chân lên không. Xem lý do đầy đủ
  // ở pin-diagnostics.h.
  pinMode(IR_RX_PIN, INPUT_PULLDOWN);
  delay(10);  // cho chân ổn định sau khi đổi cấu hình trở kéo

  uint16_t high = 0, transitions = 0;
  int last = digitalRead(IR_RX_PIN);
  for (uint16_t i = 0; i < SAMPLES; i++) {
    const int v = digitalRead(IR_RX_PIN);
    if (v) high++;
    if (v != last) transitions++;
    last = v;
    delayMicroseconds(SAMPLE_GAP_US);
  }

  // Trả chân về ngõ vào trung tính để IRrecv dùng lại bình thường.
  pinMode(IR_RX_PIN, INPUT);

  const uint8_t pct = (uint8_t)((uint32_t)high * 100u / SAMPLES);
  Serial.printf("muc CAO %u%% trong %u mau, %u lan doi muc\n", pct, SAMPLES, transitions);

  if (pct >= 90 && transitions <= 2) {
    Serial.println(
        "=> TOT. Mat thu CO nguon va DAT CO noi dung GPIO27 (nghi o muc cao).\n"
        "   Hoc mai khong duoc thi loi nam cho khac: het pin remote, chia lech\n"
        "   huong, hoac xa qua. Thu chia sat ~2cm.");
  } else if (transitions > 2) {
    Serial.println(
        "=> DANG CO TIN HIEU IR di vao. Mat thu chay tot va dang nhan duoc song\n"
        "   (co the la remote cua ban, co the la nhieu den phong).");
  } else {
    Serial.println(
        "=> HONG. Chan bi keo xuong tu do => KHONG co gi lai no len.\n"
        "   Nghia la mat thu chua toi duoc GPIO27. Kiem theo thu tu:\n"
        "     1. DAO NHAM GPIO14 <-> GPIO27? (nghi pham so mot: hong ca thu lan phat)\n"
        "     2. Module co dien chua? Do von ke VCC-GND NGAY TAI CHAN MODULE, phai ~3.3V\n"
        "     3. KY-022 thu tu la  DAT / VCC / GND  -- khong phai VCC/DAT/GND\n"
        "     4. Day DAT co that su cam vao GPIO27 khong");
  }
  Serial.println("================================\n");
}

void blinkIrEmitter() {
  Serial.printf(
      "\n=== NHAY LED PHAT (GPIO%d) trong 5 giay ===\n"
      "Mo CAMERA DIEN THOAI chia vao LED phat. Mat thuong khong thay hong ngoai\n"
      "nhung camera thi thay: LED se hien thanh cham TIM/TRANG nhap nhay.\n"
      "  Co nhap nhay  -> phan phat CHAY, loi nam o phia may xong (huong/tam/ma)\n"
      "  Toi den       -> LED khong duoc lai: dao chan, cam nguoc, hoac chua noi\n",
      IR_TX_PIN);

  pinMode(IR_TX_PIN, OUTPUT);
  for (uint8_t i = 0; i < 50; i++) {  // 50 x 100ms = 5 giay, 10Hz
    digitalWrite(IR_TX_PIN, HIGH);
    delay(50);
    digitalWrite(IR_TX_PIN, LOW);
    delay(50);
  }
  Serial.println("=== xong ===\n");
}

}  // namespace PinDiagnostics
