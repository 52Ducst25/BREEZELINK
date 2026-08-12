#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// ============================================================================
//  Đường UART giữa GATEWAY (trong nhà) và ARDUINO UNO Q (edge AI)
// ----------------------------------------------------------------------------
//  TRƯỚC ĐÂY LÀ BLUETOOTH. Bỏ vì một phép đo, không phải vì sở thích:
//
//    Gateway, BLE bật:   0,31 gói ESP-NOW/giây
//    Gateway, BLE tắt:   0,80 gói/giây   ← đúng bằng 4 node × 5 giây
//
//  Bật Bluetooth thì chip BẮT BUỘC phải bật ngủ WiFi — nó abort chứ không chạy
//  kém đi (`Should enable WiFi modem sleep when both WiFi and Bluetooth are
//  enabled`). Mà radio ngủ thì gói ESP-NOW đến đúng lúc đó là mất, không ai đệm
//  hộ, và broadcast không có ACK nên node vẫn báo "đã phát". Node ngoài trời rơi
//  ~50% và nhấp nháy ONLINE/OFFLINE; tắt BLE là vào sạch, 0 gói rơi.
//
//  Tức là BLE ăn mất ~60% khả năng thu của chính cái gateway nó phục vụ. UART
//  không đụng tới radio nên lấy lại toàn bộ. Cái giá là một sợi dây, và hai bo
//  phải nằm cạnh nhau.
//
//  MẤT LUÔN CẢ MỘT MỚ LỘN XỘN: không quét, không ghép đôi, không thương lượng
//  MTU (ảnh chụp 39 byte từng bị nghi cắt cụt vì BlueZ báo MTU 23), không NimBLE
//  ~100KB flash, không ai tranh ăng-ten 2.4GHz.
//
//  KHUÔN GÓI GIỮ NGUYÊN từ bản BLE — đó là phần đắt nhất và nó không dính gì tới
//  đường ống. Chỉ thêm CRC cho gói lệnh (xem AcUnoQCommandHeader).
//
//  ĐỒNG BỘ KHUNG: gói có kích thước cố định, mở đầu bằng `magic`, kết bằng CRC.
//  Bên nhận chờ thấy magic rồi gom đủ byte; sai thì trượt một byte tìm lại. Nhờ
//  vậy nhiễu trên dây hay cắm dây giữa chừng đều tự phục hồi.
//
//  NHỊ PHÂN CHỨ KHÔNG JSON: firmware không có bộ phân tích JSON nào rảnh cho
//  đường này (ArduinoJson đang dành cho MQTT với bộ đệm 12KB), và struct packed
//  thì hai bên đọc ra cùng byte mà không phải thoả thuận gì thêm.
//
//  BÊN PYTHON đọc chính file này bằng `struct` — xem edge-ai/edge_ai/protocol.py,
//  nơi chuỗi format được chép kèm chú thích trỏ ngược về đây. Đổi bố cục là phải
//  đổi CẢ HAI, và `version` bên dưới là thứ bắt được lúc quên.
// ============================================================================

#define AC_UNOQ_MAGIC   0xAC
// 2 = bản UART (gói lệnh có thêm crc8). Bản 1 là BLE và KHÔNG tương thích:
// gói lệnh v1 dài 12 byte, v2 dài 13. Bên nhận kiểm `version` nên hai bên lệch
// phiên bản sẽ bị từ chối thẳng thay vì đọc lệch trường rồi ra lệnh sai.
#define AC_UNOQ_VERSION 2

/// Số góc phòng gateway báo cáo được trong một ảnh chụp. 4 là lắp thật; để 8 thì
/// ảnh chụp phình lên vô ích, còn để 4 mà lắp 5 thì góc thứ 5 biến mất khỏi màn
/// UNO Q trong im lặng — nên con số này khớp đúng số ô của RoomRegistry.
#define AC_UNOQ_MAX_ROOMS 4

// UUID dịch vụ/đặc tính của bản BLE đã bỏ — UART không có khái niệm đó. Chúng
// từng ở đây; xem lịch sử git nếu cần quay lại Bluetooth.

// --- Quy ước mã hoá giá trị --------------------------------------------------
//  Nhiệt độ ×100 vào int16 (dải -327.68..327.67 °C), độ ẩm ×100 vào uint16.
//  INT16_MIN / 0xFFFF nghĩa là "KHÔNG CÓ SỐ ĐO" — không phải 0. 0 °C và 0 %RH
//  đều là giá trị hợp lệ, và hiểu 0 thành "không có" là cách nhanh nhất để một
//  cảm biến hỏng hiện thành một căn phòng 0 độ.
#define AC_UNOQ_T_INVALID ((int16_t)0x8000)
#define AC_UNOQ_H_INVALID ((uint16_t)0xFFFF)
/// Chưa từng nghe thấy máy chủ ra lệnh lần nào (khác hẳn "vừa nghe xong").
#define AC_UNOQ_SILENCE_NEVER ((uint16_t)0xFFFF)

/// Chế độ máy lạnh trên dây. Khớp app.models.enums.AcMode.
enum AcUnoQMode : uint8_t {
  AC_UNOQ_MODE_OFF     = 0,
  AC_UNOQ_MODE_COOL    = 1,
  AC_UNOQ_MODE_DRY     = 2,
  AC_UNOQ_MODE_FAN     = 3,
  AC_UNOQ_MODE_UNKNOWN = 0xFF,
};

// Cờ trạng thái trong ảnh chụp.
#define AC_UNOQ_FLAG_WIFI_UP     0x01
#define AC_UNOQ_FLAG_MQTT_UP     0x02
#define AC_UNOQ_FLAG_OVERRIDE    0x04  ///< người dùng đang giữ quyền (panel/app)
#define AC_UNOQ_FLAG_OUT_ONLINE  0x08  ///< node ngoài trời còn nhịp tim

/// Ảnh chụp gateway -> UNO Q. 39 byte.
///
/// Mang CẢ số từng góc LẪN trung vị, dù UNO Q tự tính lại được: trung vị là con
/// số gateway ĐANG hiển thị trên tường, và nếu UNO Q tự tính ra một con khác thì
/// phải thấy được sự lệch đó chứ không phải im lặng chọn một bên.
typedef struct __attribute__((packed)) {
  uint8_t  magic;          // = AC_UNOQ_MAGIC
  uint8_t  version;        // = AC_UNOQ_VERSION
  uint8_t  room_count;     // số góc CÓ SỐ ĐO hợp lệ, đang tham gia trung vị
  uint8_t  flags;          // AC_UNOQ_FLAG_*

  int16_t  t_in_c100;      // trung vị các góc còn tươi
  uint16_t h_in_x100;
  int16_t  t_out_c100;
  uint16_t h_out_x100;

  int16_t  room_t_c100[AC_UNOQ_MAX_ROOMS];
  uint16_t room_h_x100[AC_UNOQ_MAX_ROOMS];
  uint8_t  room_corner[AC_UNOQ_MAX_ROOMS];   // nhãn góc; AC_CORNER_NONE = ô trống

  /// Giây kể từ lệnh cuối cùng máy chủ gửi xuống. ĐÂY LÀ TRƯỜNG QUAN TRỌNG NHẤT
  /// của cả gói: nó là thứ duy nhất cho UNO Q biết cloud còn sống hay không.
  /// Gateway biết chắc chắn hơn UNO Q, vì chính nó giữ phiên MQTT.
  uint16_t cloud_silence_sec;

  uint8_t  ac_mode;        // AcUnoQMode — trạng thái THẬT đã bắn ra máy lạnh
  int8_t   ac_setpoint;    // °C, -1 = chưa biết
  uint16_t uptime_min;
  uint8_t  crc8;
} AcUnoQSnapshot;

/// Lệnh/đề xuất UNO Q -> gateway. 13 byte.
typedef struct __attribute__((packed)) {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  kind;       // AC_UNOQ_KIND_*
  uint8_t  mode;       // AcUnoQMode
  int8_t   setpoint;   // °C, -1 nếu chế độ không cần
  uint8_t  reserved;   // giữ chỗ cho căn byte; phải = 0
  uint16_t seq;        // tăng mỗi lệnh — gateway bỏ bản lặp
  uint32_t link_key;   // = acEspNowSiteKey(ORG_ID), xem chú thích bên dưới
  uint8_t  crc8;       // Dallas/Maxim trên 12 byte đứng trước
} AcUnoQCommandHeader;

/// VÌ SAO GÓI LỆNH CÓ CRC CÒN BẢN BLE THÌ KHÔNG:
///
/// Bản BLE dựa vào tầng liên kết của Bluetooth để đảm bảo toàn vẹn — hợp lý, vì
/// BLE tự kiểm CRC24 mỗi gói và loại gói hỏng trước khi tới tầng ứng dụng.
/// UART KHÔNG CÓ GÌ NHƯ VẬY: dây nhiễu, cắm rút giữa chừng, hai đầu lệch baud —
/// tất cả đều đẩy byte rác lên thẳng.
///
/// Với ảnh chụp thì một bit lật chỉ làm sai một số đo trong một nhịp. Với LỆNH
/// thì nó đổi `setpoint` hoặc `mode` rồi đi thẳng ra máy lạnh, và không có ai
/// đứng giữa để thấy sai. Nên gói lệnh phải tự chứng minh mình nguyên vẹn.

/// `kind` TÁCH ĐỀ XUẤT KHỎI LỆNH, và đây là ranh giới quan trọng nhất của giao
/// thức này: bình thường UNO Q chỉ ADVICE (gateway ghi log + hiện lên màn, KHÔNG
/// bắn IR), chỉ khi cloud im lặng đủ lâu nó mới COMMAND. Gộp hai cái làm một
/// nghĩa là mọi tính toán thử nghiệm trên UNO Q đều đi thẳng ra máy nén.
#define AC_UNOQ_KIND_ADVICE  0
#define AC_UNOQ_KIND_COMMAND 1

/// `link_key` băm từ ORG_ID, cùng thủ thuật với site_key của gói BLE cảm biến
/// từng cân nhắc.
///
/// ĐÂY KHÔNG PHẢI XÁC THỰC. Nó chặn được: một UNO Q của hộ khác vô tình kết nối
/// nhầm gateway trong chung cư, và đồ chơi BLE ghi bừa vào đặc tính. Nó KHÔNG
/// chặn được kẻ cố ý — khoá nằm sẵn trong config.h và đi trần trên sóng.
///
/// Cách siết thật khi cần: bật bonding + passkey tĩnh của NimBLE
/// (`NimBLEDevice::setSecurityAuth(true, true, true)` + `setSecurityPasskey`),
/// rồi ghép đôi một lần lúc lắp. Chưa làm vì nó thêm một bước lắp đặt có thể sai,
/// và mối đe doạ ở đây (ai đó trong tầm 10m muốn chỉnh máy lạnh nhà bạn) không
/// tương xứng.
static inline uint32_t acUnoQLinkKey(const char *orgId) {
  uint32_t hash = 2166136261u;   // FNV-1a 32-bit
  for (const char *p = orgId; p && *p; p++) {
    hash ^= (uint8_t)*p;
    hash *= 16777619u;
  }
  return hash;
}

/// CRC8 Dallas/Maxim (đa thức phản chiếu 0x8C).
///
/// CẦN dù BLE đã có CRC24 ở tầng link: CRC của BLE bảo vệ đường truyền, nó không
/// nói gì về việc gói này có đúng bố cục mình đang chờ hay không. Đây là chốt
/// chặn cuối trước khi hai byte nào đó được đọc ra thành nhiệt độ phòng.
static inline uint8_t acUnoQCrc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    uint8_t inbyte = data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      const uint8_t mix = (uint8_t)((crc ^ inbyte) & 0x01);
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

static inline void acUnoQSealSnapshot(AcUnoQSnapshot *s) {
  s->crc8 = acUnoQCrc8((const uint8_t *)s, (uint8_t)(sizeof(*s) - 1));
}

static inline void acUnoQSealCommand(AcUnoQCommandHeader *c) {
  c->crc8 = acUnoQCrc8((const uint8_t *)c, (uint8_t)(sizeof(*c) - 1));
}

/// Gói lệnh có nguyên vẹn không. Bên GỬI seal, bên NHẬN check — cặp này phải đi
/// cùng nhau, quên một bên là mọi lệnh bị từ chối (hoặc tệ hơn: mọi lệnh được
/// nhận kể cả gói hỏng).
static inline bool acUnoQCheckCommand(const AcUnoQCommandHeader *c) {
  return c->crc8 == acUnoQCrc8((const uint8_t *)c, (uint8_t)(sizeof(*c) - 1));
}

static inline bool acUnoQCheckSnapshot(const AcUnoQSnapshot *s) {
  return s->crc8 == acUnoQCrc8((const uint8_t *)s, (uint8_t)(sizeof(*s) - 1));
}

// --- mã hoá / giải mã giá trị ------------------------------------------------

static inline int16_t acUnoQEncodeTemp(float celsius) {
  if (!(celsius > -300.0f && celsius < 300.0f)) return AC_UNOQ_T_INVALID;
  return (int16_t)(celsius * 100.0f);
}

static inline uint16_t acUnoQEncodeRh(float percent) {
  if (!(percent >= 0.0f && percent <= 100.0f)) return AC_UNOQ_H_INVALID;
  return (uint16_t)(percent * 100.0f);
}

/// MTU tối thiểu để một ảnh chụp đi trọn trong MỘT gói notify (ATT tốn 3 byte
/// header). Dưới ngưỡng này thì notify bị CẮT CỤT TRONG IM LẶNG — không lỗi,
/// không cảnh báo, chỉ là mấy góc cuối biến mất. Gateway phải kiểm và kêu to.
#define AC_UNOQ_MIN_MTU (sizeof(AcUnoQSnapshot) + 3)

// ---------------------------------------------------------------------------
//  CHỐT KÍCH THƯỚC — đây là thứ bắt được lỗi đồng bộ hai bên NGAY LÚC BIÊN DỊCH.
//
//  Bố cục này có một bản song sinh bằng Python ở edge-ai/edge_ai/protocol.py.
//  Thêm một trường ở đây mà quên bên kia thì gói vẫn "giải mã thành công" — chỉ
//  là mọi trường sau chỗ chèn đều lệch, và nhiệt độ phòng đọc ra thành số rác.
//  CRC không cứu được: nó được tính trên đúng số byte mà bên gửi nghĩ là đúng.
//
//  Hai con số dưới đây phải khớp SNAPSHOT_SIZE và COMMAND_SIZE mà protocol.py
//  in ra. Đổi bố cục -> sửa cả ba chỗ, và dòng static_assert này không cho quên.
// ---------------------------------------------------------------------------
#ifdef __cplusplus
static_assert(sizeof(AcUnoQSnapshot) == 39,
              "AcUnoQSnapshot doi kich thuoc — cap nhat edge-ai/edge_ai/protocol.py cho khop");
static_assert(sizeof(AcUnoQCommandHeader) == 13,
              "AcUnoQCommandHeader doi kich thuoc — cap nhat edge-ai/edge_ai/protocol.py cho khop");
#endif
