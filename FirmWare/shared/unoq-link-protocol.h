#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// ============================================================================
//  Đường Bluetooth giữa GATEWAY (trong nhà) và ARDUINO UNO Q (edge AI)
// ----------------------------------------------------------------------------
//  AI LÀ AI:
//    gateway = PERIPHERAL (GATT server) — nó quảng bá và chờ
//    UNO Q   = CENTRAL (GATT client)    — nó quét, kết nối, đọc/ghi
//
//  Chia vai như vậy vì hai lý do, không phải quy ước tuỳ tiện:
//    1. UNO Q chạy Debian + BlueZ, một central đầy đủ và dễ lập trình (bleak).
//       ESP32 làm peripheral là khuôn mẫu chuẩn và nhẹ nhất cho nó.
//    2. Gateway phải luôn sẵn sàng cho MỘT client bất kỳ đến lấy dữ liệu. Nếu
//       gateway đi tìm UNO Q thì mỗi lần UNO Q khởi động lại, gateway phải quét
//       và kết nối lại — mà quét BLE trên gateway là thứ tranh sóng trực tiếp
//       với WiFi/MQTT, đường DUY NHẤT để lệnh máy lạnh đi xuống.
//
//  VÌ SAO GATT CHỨ KHÔNG PHẢI ADVERTISING NHƯ CÁC NODE CẢM BIẾN:
//    đây là đường HAI CHIỀU — UNO Q phải gửi lệnh ngược về gateway. Advertising
//    chỉ đi một chiều, và trần 31 byte của nó không chở nổi ảnh chụp 4 góc phòng.
//    GATT có MTU thương lượng được tới hàng trăm byte và có kênh ghi ngược.
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
#define AC_UNOQ_VERSION 1

/// Số góc phòng gateway báo cáo được trong một ảnh chụp. 4 là lắp thật; để 8 thì
/// ảnh chụp phình lên vô ích, còn để 4 mà lắp 5 thì góc thứ 5 biến mất khỏi màn
/// UNO Q trong im lặng — nên con số này khớp đúng số ô của RoomRegistry.
#define AC_UNOQ_MAX_ROOMS 4

// --- UUID dịch vụ + đặc tính -------------------------------------------------
//  UUID 128-bit riêng, KHÔNG mượn UUID 16-bit chuẩn của Bluetooth SIG: dịch vụ
//  này không phải "Environmental Sensing" chuẩn, và giả làm nó sẽ khiến mọi app
//  BLE phổ thông diễn giải sai các byte.
#define AC_UNOQ_SERVICE_UUID   "4f1c9a00-1c3e-4d5a-9b21-7e6d0a3f8c10"
/// Ảnh chụp gateway -> UNO Q. READ + NOTIFY.
#define AC_UNOQ_SNAPSHOT_UUID  "4f1c9a01-1c3e-4d5a-9b21-7e6d0a3f8c10"
/// Lệnh/đề xuất UNO Q -> gateway. WRITE.
#define AC_UNOQ_COMMAND_UUID   "4f1c9a02-1c3e-4d5a-9b21-7e6d0a3f8c10"

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

/// Ảnh chụp gateway -> UNO Q. 44 byte.
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

/// Lệnh/đề xuất UNO Q -> gateway. 12 byte.
typedef struct __attribute__((packed)) {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  kind;       // AC_UNOQ_KIND_*
  uint8_t  mode;       // AcUnoQMode
  int8_t   setpoint;   // °C, -1 nếu chế độ không cần
  uint8_t  reserved;   // giữ chỗ cho căn byte; phải = 0
  uint16_t seq;        // tăng mỗi lệnh — gateway bỏ bản lặp
  uint32_t link_key;   // = acEspNowSiteKey(ORG_ID), xem chú thích bên dưới
} AcUnoQCommandHeader;

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
static_assert(sizeof(AcUnoQCommandHeader) == 12,
              "AcUnoQCommandHeader doi kich thuoc — cap nhat edge-ai/edge_ai/protocol.py cho khop");
#endif
