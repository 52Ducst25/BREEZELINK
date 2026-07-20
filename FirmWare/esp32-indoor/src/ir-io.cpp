#include "ir-io.h"
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <string.h>

namespace IrIo {

/// Bộ đệm thu. Khung điều hoà dài gấp nhiều lần remote TV nên 1024 là mức thư
/// viện khuyến nghị riêng cho máy lạnh; để mặc định (~100) sẽ bắt thiếu đuôi.
static const uint16_t CAPTURE_BUFFER = 1024;

/// Khoảng lặng đủ để kết luận "hết khung" (ms). Remote điều hoà có quãng nghỉ
/// giữa các cụm dài hơn remote thường — để 15ms như mặc định thì một lần bấm bị
/// cắt thành hai khung, học ra mã cụt.
static const uint8_t CAPTURE_TIMEOUT_MS = 50;

/// Sóng mang chuẩn của remote gia dụng.
static const uint16_t CARRIER_KHZ = 38;

/// Dưới ngưỡng này gần như chắc chắn là nhiễu (đèn huỳnh quang, remote khác
/// lướt qua), không phải một lần bấm thật. Khung điều hoà ngắn nhất cũng vài
/// chục mốc. Bỏ qua và học tiếp thay vì gửi rác lên backend.
static const uint16_t MIN_RAW_LEN = 20;

static IRsend *sender = nullptr;
static IRrecv *receiver = nullptr;
static decode_results results;

static bool     active = false;
static bool     timedOut = false;
static uint32_t deadline = 0;

void begin(uint8_t txPin, uint8_t rxPin) {
  sender = new IRsend(txPin);
  sender->begin();
  // Cấp phát thôi, CHƯA enableIRIn() — mắt thu chỉ bật khi vào chế độ học.
  receiver = new IRrecv(rxPin, CAPTURE_BUFFER, CAPTURE_TIMEOUT_MS, true);
}

void blast(const uint16_t *raw, uint16_t len) {
  if (sender == nullptr || raw == nullptr || len == 0) return;

  // Nếu đang học mà có lệnh phát chen vào: tắt mắt thu trong lúc bắn, không thì
  // node bắt lại chính khung của mình và tưởng đó là remote người dùng bấm.
  bool wasLearning = active;
  if (wasLearning && receiver) receiver->disableIRIn();

  sender->sendRaw(raw, len, CARRIER_KHZ);

  if (wasLearning && receiver) {
    receiver->enableIRIn();
    receiver->resume();
  }
}

void learnStart(uint32_t timeoutMs) {
  if (receiver == nullptr) return;
  receiver->enableIRIn();
  receiver->resume();
  active   = true;
  timedOut = false;
  deadline = millis() + timeoutMs;
}

bool learning() { return active; }

void learnStop() {
  if (receiver) receiver->disableIRIn();
  active = false;
}

bool learnTimedOut() {
  bool t = timedOut;
  timedOut = false;   // một lần rồi thôi, khỏi báo lặp mỗi vòng loop()
  return t;
}

/// Hết giờ chờ chưa? Trừ số học có dấu để vẫn đúng khi millis() tràn (~49 ngày).
static bool expired() { return (int32_t)(millis() - deadline) >= 0; }

uint16_t learnPoll(uint16_t *out, uint16_t maxLen) {
  if (!active || receiver == nullptr || out == nullptr) return 0;

  if (!receiver->decode(&results)) {
    if (expired()) {
      timedOut = true;
      learnStop();
    }
    return 0;
  }

  // resultToRawArray() trả mảng micro-giây đã bù kMarkExcess và đã BỎ khoảng
  // lặng dẫn đầu — đúng khuôn "mark/space xen kẽ" mà backend lưu vào
  // ir_codes.raw_timing và gửi ngược lại trong ir_raw. Mảng cấp phát bằng new[],
  // phải tự delete[].
  uint16_t len = getCorrectedRawLength(&results);
  uint16_t *raw = resultToRawArray(&results);
  uint16_t taken = 0;

  if (raw == nullptr) {
    Serial.println("[ir] het RAM khi doc khung — bam lai remote");
  } else if (len < MIN_RAW_LEN) {
    Serial.printf("[ir] bo qua nhieu (%u moc) — dang cho ban bam remote\n", len);
  } else if (len > maxLen) {
    Serial.printf("[ir] khung %u moc vuot gioi han %u — bo qua (khung cat doi la ma sai)\n",
                  len, maxLen);
  } else {
    memcpy(out, raw, (size_t)len * sizeof(uint16_t));
    taken = len;
  }
  if (raw != nullptr) delete[] raw;

  if (taken > 0) {
    learnStop();
    return taken;
  }

  receiver->resume();
  if (expired()) {
    timedOut = true;
    learnStop();
  }
  return 0;
}

} // namespace IrIo
