#include "ir-remote.h"

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <string.h>

namespace IrRemote {
namespace {

/// Bộ đệm thu của thư viện. 1024 là mức thư viện khuyến nghị; để mặc định
/// (~100) là bắt thiếu đuôi khung.
const uint16_t CAPTURE_BUFFER = 1024;

/// Khoảng lặng đủ để kết luận "hết khung" (ms).
const uint8_t CAPTURE_TIMEOUT_MS = 50;

/// Sóng mang chuẩn của remote gia dụng.
const uint16_t CARRIER_KHZ = 38;

/// Dưới ngưỡng này gần như chắc chắn là nhiễu (đèn huỳnh quang, remote khác
/// lướt qua), không phải một lần bấm thật. Bỏ qua và học tiếp thay vì lưu rác
/// vào NVS — mã rác học được thì mọi lần bắn sau đều vô hiệu, và triệu chứng
/// duy nhất là "máy không nhúc nhích" y hệt như chưa hàn LED.
const uint16_t MIN_RAW_LEN = 20;

IRsend *g_sender = nullptr;
IRrecv *g_receiver = nullptr;
decode_results g_results;

bool     g_active = false;
bool     g_timedOut = false;
uint32_t g_deadline = 0;

char     g_protocol[20] = "-";
uint16_t g_bits = 0;

/// Hết giờ chờ chưa? Trừ số học CÓ DẤU để vẫn đúng khi millis() tràn (~49 ngày).
bool expired() { return (int32_t)(millis() - g_deadline) >= 0; }

}  // namespace

void begin(uint8_t txPin, uint8_t rxPin) {
  g_sender = new IRsend(txPin);
  g_sender->begin();
  // Cấp phát thôi, CHƯA enableIRIn() — mắt thu chỉ bật khi vào chế độ học.
  // Bật thường trực thì LED phát nằm cùng bo sẽ được chính mắt thu này nghe
  // lại, và bo tưởng có người vừa bấm remote.
  g_receiver = new IRrecv(rxPin, CAPTURE_BUFFER, CAPTURE_TIMEOUT_MS, true);
}

void blast(const uint16_t *raw, uint16_t len) {
  if (g_sender == nullptr || raw == nullptr || len == 0) return;

  // Nếu đang học mà có lệnh phát chen vào: tắt mắt thu trong lúc bắn, không thì
  // bo bắt lại chính khung của mình và tưởng đó là remote người dùng bấm.
  const bool wasLearning = g_active;
  if (wasLearning && g_receiver) g_receiver->disableIRIn();

  g_sender->sendRaw(raw, len, CARRIER_KHZ);

  if (wasLearning && g_receiver) {
    g_receiver->enableIRIn();
    g_receiver->resume();
  }
}

void learnStart(uint32_t timeoutMs) {
  if (g_receiver == nullptr) return;

  // TẮT TRƯỚC NẾU ĐANG HỌC DỞ.
  //
  // enableIRIn() của IRremoteESP8266 gọi timerBegin() + timerAttachInterrupt()
  // + attachInterrupt() MỖI LẦN, không tự kiểm đã gắn hay chưa. Gọi chồng lên
  // nhau thì ESP-IDF từ chối:
  //     addApbChangeCallback(): duplicate func=...
  //     timer_group: timer_isr_callback_add(236): register interrupt service failed
  // và bộ định thời lấy mẫu KHÔNG chạy -> mắt thu CHẾT HẲN cho tới khi khởi
  // động lại, trong khi log vẫn thản nhiên in "dang cho ban bam remote". Hỏng
  // câm đúng kiểu tệ nhất: người lắp bấm remote mãi không được và sẽ đi nghi
  // mắt thu, nghi dây, nghi khoảng cách.
  //
  // Vào lại chế độ học khi đang học dở KHÔNG hiếm — bấm nhầm nút BOOT hai lần
  // là đủ. disableIRIn() dọn sạch (timerEnd + detachInterrupt) nên gọi lại an
  // toàn. (Bài học của ../../esp32-s3-panel/src/ir-io.cpp.)
  if (g_active) g_receiver->disableIRIn();

  g_receiver->enableIRIn();
  g_receiver->resume();
  g_active = true;
  g_timedOut = false;
  g_deadline = millis() + timeoutMs;
}

bool learning() { return g_active; }

uint32_t learnRemainingMs() {
  if (!g_active) return 0;
  const int32_t left = (int32_t)(g_deadline - millis());
  return left > 0 ? (uint32_t)left : 0;
}

void learnStop() {
  if (g_receiver) g_receiver->disableIRIn();
  g_active = false;
}

bool learnTimedOut() {
  const bool t = g_timedOut;
  g_timedOut = false;  // một lần rồi thôi, khỏi báo lặp mỗi vòng loop()
  return t;
}

const char *lastProtocol() { return g_protocol; }
uint16_t    lastBits() { return g_bits; }

uint16_t learnPoll(uint16_t *out, uint16_t maxLen) {
  if (!g_active || g_receiver == nullptr || out == nullptr) return 0;

  if (!g_receiver->decode(&g_results)) {
    if (expired()) {
      g_timedOut = true;
      learnStop();
    }
    return 0;
  }

  // resultToRawArray() trả mảng micro-giây đã bù kMarkExcess và đã BỎ khoảng
  // lặng dẫn đầu — đúng khuôn "mark/space xen kẽ" mà sendRaw() cần. Mảng cấp
  // phát bằng new[], PHẢI tự delete[].
  const uint16_t len = getCorrectedRawLength(&g_results);
  uint16_t *raw = resultToRawArray(&g_results);
  uint16_t taken = 0;

  if (raw == nullptr) {
    Serial.println("[ir] het RAM khi doc khung - bam lai remote");
  } else if (len < MIN_RAW_LEN) {
    Serial.printf("[ir] bo qua nhieu (%u moc) - dang cho ban bam remote\n", len);
  } else if (len > maxLen) {
    Serial.printf(
        "[ir] khung %u moc vuot gioi han %u - bo qua.\n"
        "     Dai co the la ban dang chia REMOTE DIEU HOA vao day chu khong "
        "phai remote may xong.\n",
        len, maxLen);
  } else {
    memcpy(out, raw, (size_t)len * sizeof(uint16_t));
    taken = len;

    // Ghi lại thư viện nhận ra giao thức gì. KHÔNG dùng để chấp nhận hay từ
    // chối khung — ta vẫn phát lại nguyên văn nên giao thức lạ cũng chạy. Đây
    // thuần tuý là BẰNG CHỨNG cho người lắp: nhiễu đèn phòng gần như không bao
    // giờ giải mã ra một giao thức có tên, còn remote gia dụng thì gần như
    // luôn ra NEC. Không có con số này thì "60 mốc" là tất cả những gì biết
    // được, mà 60 mốc thì vừa có thể là remote vừa có thể là rác.
    const String name = typeToString(g_results.decode_type, g_results.repeat);
    strncpy(g_protocol, name.c_str(), sizeof(g_protocol) - 1);
    g_protocol[sizeof(g_protocol) - 1] = '\0';
    g_bits = g_results.bits;
  }
  if (raw != nullptr) delete[] raw;

  if (taken > 0) {
    learnStop();
    return taken;
  }

  g_receiver->resume();
  if (expired()) {
    g_timedOut = true;
    learnStop();
  }
  return 0;
}

}  // namespace IrRemote
