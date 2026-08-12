/*
  BreezeLink — cầu nối giữa gateway ESP32-S3 và nửa Linux của UNO Q.

  ĐÂY LÀ BẢN CHẠY THẬT, khác sketch thử ở một điểm: nó không chỉ in ra màn hình
  mà ĐẨY DỮ LIỆU sang Python. Thuật toán điều khiển và phần AI chạy bên đó.

    ESP32-S3 ──UART D0/D1──► sketch này ──RPC──► arduino-router ──► Python
                            (kiểm khung)                          (quyết định)

  ĐẤU DÂY (đối chiếu ABX00162-full-pinout.pdf):
     ESP32-S3 GPIO18 (TX) ──► D0 = PB7 = USART1_RX
     ESP32-S3 GPIO17 (RX) ◄── D1 = PB6 = USART1_TX
     GND ─── GND

  ĐỪNG NỐI VÀO CHÂN GHI "RX"/"TX" trên hàng chân kia: đó là SOC_SE4_RX/TX, đi
  thẳng vào Qualcomm và chạy 1.8V. Nối 3.3V vào đó là hỏng chân SoC — và vì hàng
  chân số KHÔNG có chữ RX/TX nào nên đây là chỗ rất dễ cắm nhầm.

  ---------------------------------------------------------------------------
  SKETCH NÀY CỐ Ý KHÔNG HIỂU GÓI. Nó kiểm khung (magic + version + CRC) rồi đẩy
  nguyên 39 byte sang Python dưới dạng hex. Bố cục gói vì thế chỉ nằm ở hai chỗ
  đã chốt với nhau bằng static_assert — struct C và protocol.py — chứ không phát
  sinh chỗ thứ ba ở đây phải nhớ sửa theo.

  KÈO THEO: sketch không cần biết ORG_ID. link_key băm từ nó được tính bên
  Python, nên thư mục app này đem đi đâu cũng không mang theo định danh hộ nào.

  VẪN KIỂM CRC dù Python cũng kiểm: một khung hỏng bị chặn ở đây tốn 0 byte RPC,
  còn để lọt thì nó chiếm một lượt gọi qua router rồi mới bị bỏ. Quan trọng hơn,
  bộ đếm `g_bad` ngay dưới đây phân biệt được "dây nhiễu" với "RPC chết", mà đó
  là hai hỏng hóc cần sửa theo hai hướng hoàn toàn khác nhau.
*/

#include <Arduino_RouterBridge.h>

#include "led-matrix-logo.h"
#include "unoq-link-protocol.h"

// Serial1 = USART1 = D0/D1. KHÔNG dùng Serial: router đã chiếm cổng đó để nói
// chuyện với nửa Linux, nên chỉ số dịch đi một — Serial1 mới là hàng chân số.
#define GW_SERIAL Serial1
#define GW_BAUD   115200

// Tên phương thức RPC. PHẢI KHỚP edge_ai/bridge_client.py. Sai tên thì router
// định tuyến vào hư không và KHÔNG BÁO LỖI — notify là fire-and-forget, bên kia
// chỉ đơn giản không bao giờ được gọi, y hệt triệu chứng đứt dây.
static const char *RPC_SNAPSHOT = "gw/snapshot";
static const char *RPC_COMMAND  = "gw/command";

static uint8_t  g_buf[sizeof(AcUnoQSnapshot)];
static uint8_t  g_len = 0;

static uint32_t g_ok = 0;        // khung hợp lệ đã đẩy sang Python
static uint32_t g_bad = 0;       // khung sai magic/version/CRC
static uint32_t g_bytes = 0;     // tổng byte thô đọc từ dây
static uint32_t g_cmd = 0;       // lệnh Python gửi xuống, đã ghi ra dây
static uint32_t g_cmdBad = 0;    // lệnh Python gửi xuống nhưng hỏng
static uint32_t g_lastOkMs = 0;

// KHÔNG đặt tên là HEX: Arduino có sẵn `#define HEX 16` cho Print, nên khai báo
// trùng tên sẽ nở ra `static const char 16[]` và lỗi biên dịch ở một dòng chẳng
// liên quan gì tới chỗ thật sự sai.
static const char HEXDIG[] = "0123456789abcdef";

/// Đẩy một khung đã kiểm sang Python.
static void pushSnapshot() {
  // +1 cho ký tự kết chuỗi. Đệm TĨNH chứ không phải String cộng dồn: hàm này
  // chạy mỗi 5 giây suốt nhiều tháng, và nối chuỗi 78 lần mỗi lượt là 78 lần
  // cấp phát lại trên một con vi điều khiển không có ai dọn phân mảnh hộ.
  static char hex[sizeof(g_buf) * 2 + 1];
  for (uint8_t i = 0; i < sizeof(g_buf); i++) {
    hex[i * 2]     = HEXDIG[g_buf[i] >> 4];
    hex[i * 2 + 1] = HEXDIG[g_buf[i] & 0x0F];
  }
  hex[sizeof(g_buf) * 2] = '\0';

  Bridge.notify(RPC_SNAPSHOT, String(hex));
}

static void reportFrame(const AcUnoQSnapshot &s) {
  Monitor.print("[rx] phong=");
  Monitor.print(s.room_count);

  Monitor.print("  t_in=");
  if (s.t_in_c100 == AC_UNOQ_T_INVALID) Monitor.print("--");
  else Monitor.print(s.t_in_c100 / 100.0f, 1);

  Monitor.print("  t_out=");
  if (s.t_out_c100 == AC_UNOQ_T_INVALID) Monitor.print("--");
  else Monitor.print(s.t_out_c100 / 100.0f, 1);

  Monitor.print("  co=0x");
  Monitor.print(s.flags, HEX);
  Monitor.print("  im_lang=");
  if (s.cloud_silence_sec == AC_UNOQ_SILENCE_NEVER) Monitor.print("chua-tung");
  else { Monitor.print(s.cloud_silence_sec); Monitor.print("s"); }
  Monitor.print("  #");
  Monitor.println(g_ok);
}

/// Bóc khung đã đủ byte. Trả false nếu không hợp lệ.
static bool decodeFrame() {
  AcUnoQSnapshot s;
  memcpy(&s, g_buf, sizeof(s));

  if (s.magic != AC_UNOQ_MAGIC) return false;
  if (s.version != AC_UNOQ_VERSION) {
    Monitor.print("[!] gateway gui version ");
    Monitor.print(s.version);
    Monitor.print(", sketch hieu ");
    Monitor.print(AC_UNOQ_VERSION);
    Monitor.println(" — nap lai mot ben");
    return false;
  }
  if (!acUnoQCheckSnapshot(&s)) return false;

  g_ok++;
  g_lastOkMs = millis();
  pushSnapshot();
  reportFrame(s);
  return true;
}

static void pump() {
  while (GW_SERIAL.available() > 0) {
    const uint8_t b = (uint8_t)GW_SERIAL.read();
    g_bytes++;

    if (g_len == 0 && b != AC_UNOQ_MAGIC) continue;   // chờ magic
    g_buf[g_len++] = b;
    if (g_len < sizeof(g_buf)) continue;

    if (decodeFrame()) {
      g_len = 0;
    } else {
      g_bad++;
      // Trượt MỘT byte rồi tìm magic tiếp, KHÔNG xoá sạch đệm: magic thật có thể
      // nằm ngay trong đám vừa gom (nửa khung cũ dính nửa khung mới), và xoá sạch
      // là mất luôn khung tốt đứng ngay sau khung hỏng.
      memmove(g_buf, g_buf + 1, sizeof(g_buf) - 1);
      g_len = sizeof(g_buf) - 1;
      while (g_len > 0 && g_buf[0] != AC_UNOQ_MAGIC) {
        memmove(g_buf, g_buf + 1, g_len - 1);
        g_len--;
      }
    }
  }
}

static int8_t nibble(char c) {
  if (c >= '0' && c <= '9') return (int8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
  return -1;
}

/// Python gọi hàm này để gửi một đề xuất/lệnh xuống gateway.
///
/// CHẠY TRÊN LUỒNG CỦA BRIDGE, mà luồng đó chỉ có 500 byte ngăn xếp — nên mọi
/// thứ ở đây phải nhỏ và không đệ quy. 13 byte trên ngăn xếp là vừa.
static void onCommand(String hex) {
  const size_t need = sizeof(AcUnoQCommandHeader) * 2;
  uint8_t out[sizeof(AcUnoQCommandHeader)];

  if (hex.length() != need) {
    g_cmdBad++;
    return;
  }
  for (size_t i = 0; i < sizeof(out); i++) {
    const int8_t hi = nibble(hex[i * 2]);
    const int8_t lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      g_cmdBad++;
      return;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }

  // Kiểm TRƯỚC KHI ghi ra dây. Gateway cũng kiểm và sẽ từ chối gói hỏng, nhưng
  // một lệnh hỏng lọt xuống đó chỉ hiện ra ở log gateway — nơi không ai đang
  // nhìn lúc gỡ lỗi phía này.
  AcUnoQCommandHeader c;
  memcpy(&c, out, sizeof(c));
  if (c.magic != AC_UNOQ_MAGIC || c.version != AC_UNOQ_VERSION || !acUnoQCheckCommand(&c)) {
    g_cmdBad++;
    return;
  }

  GW_SERIAL.write(out, sizeof(out));
  g_cmd++;

  Monitor.print("[tx] ");
  Monitor.print(c.kind == AC_UNOQ_KIND_COMMAND ? "LENH" : "de-xuat");
  // Ép về int: `setpoint` là int8_t, mà Print có nạp chồng riêng cho `char` —
  // để nguyên thì 25 in ra thành một ký tự điều khiển chứ không phải "25".
  Monitor.print(" mode=");
  Monitor.print((int)c.mode);
  Monitor.print(" set=");
  Monitor.print((int)c.setpoint);
  Monitor.print(" seq=");
  Monitor.println(c.seq);
}

void setup() {
  Monitor.begin(115200);
  // Bridge phải lên TRƯỚC: không có nó thì Monitor không có đường về Python và
  // mọi dòng log dưới đây rơi vào hư không.
  Bridge.begin();
  while (!Monitor) {
    delay(200);
  }

  GW_SERIAL.begin(GW_BAUD);
  Bridge.provide(RPC_COMMAND, onCommand);
  LedLogo::begin();

  Monitor.println("== BreezeLink · cau noi UART <-> Python ==");
  Monitor.print("cho khung ");
  Monitor.print((unsigned)sizeof(AcUnoQSnapshot));
  Monitor.println(" byte tren D0/D1 @115200");
}

void loop() {
  // ĐỌC UART TRƯỚC. Đệm UART của Zephyr có hạn, và một khung 39 byte tới trong
  // lúc đang bận dựng hình cho ma trận LED là một khung mất.
  pump();
  LedLogo::update();

  // Tổng kết mỗi 30 giây — thưa hơn sketch thử, vì ở bản chạy thật thì log
  // Python mới là thứ cần đọc, và một dòng tổng kết mỗi 5 giây sẽ đẩy nó trôi.
  static uint32_t lastReport = 0;
  if (millis() - lastReport >= 30000) {
    lastReport = millis();
    Monitor.print("[tong] byte=");
    Monitor.print(g_bytes);
    Monitor.print("  khung=");
    Monitor.print(g_ok);
    Monitor.print("  hong=");
    Monitor.print(g_bad);
    Monitor.print("  lenh=");
    Monitor.print(g_cmd);
    if (g_cmdBad) {
      Monitor.print("(+");
      Monitor.print(g_cmdBad);
      Monitor.print(" hong)");
    }
    // Phân biệt ba ca hỏng khác hẳn nhau, vì cách sửa khác hẳn nhau.
    if (g_bytes == 0) {
      Monitor.println("  <- KHONG CO BYTE NAO: kiem day S3 GPIO18 -> D0, GND chung, S3 da chay chua");
    } else if (g_ok == 0) {
      Monitor.println("  <- CO BYTE MA KHONG RA KHUNG: gan nhu chac chan lech baud hoac thieu GND");
    } else {
      Monitor.print("  lan cuoi ");
      Monitor.print((millis() - g_lastOkMs) / 1000);
      Monitor.println("s truoc");
    }
  }

  delay(5);
}
