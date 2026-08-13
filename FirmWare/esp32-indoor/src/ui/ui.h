#pragma once
#include <Arduino.h>

// ============================================================================
//  Giao diện màn cảm ứng 2.8" của node TRONG NHÀ — chạy trên TÁC VỤ RIÊNG.
// ----------------------------------------------------------------------------
//  Thiết kế đầy đủ (wireframe, toạ độ, bản đồ chạm, lý do từng quyết định):
//  ../../Interface/README.md
//
//  VÌ SAO PHẢI LÀ HAI TÁC VỤ TRÊN HAI LÕI, KHÔNG PHẢI "GỌI TRONG loop()":
//
//  1. loop() ĐỨNG HÌNH HÀNG GIÂY, KHÔNG PHẢI HÃN HỮU MÀ LÀ BÌNH THƯỜNG:
//       connectWifi()  vòng while + delay(500) tới khi vào được mạng
//       connectMqtt()  delay(2000) mỗi lần rc != 0
//       IrIo::blast()  chặn 50-250ms
//     Vẽ trong loop() thì đúng lúc mất mạng — lúc người dùng cần nhìn màn nhất
//     — màn đứng im như node đã chết. Đó là kiểu hỏng tệ nhất của một bảng điều
//     khiển treo tường: nó khẳng định sai.
//
//  2. LÕI 0 CHO GIAO DIỆN, LÕI 1 CHO IR — BẮT BUỘC, KHÔNG PHẢI ĐỂ CHO MƯỢT:
//     IrIo::blast() tự đếm nhịp bằng delayMicroseconds để dựng sóng mang 38kHz
//     (chu kỳ 26µs). Một tác vụ khác cùng lõi bị bộ lập lịch xen vào giữa là
//     mark/space giãn ra vài chục µs -> khung IR sai -> máy lạnh im lặng không
//     phản ứng, mà log vẫn báo "da phat". Arduino chạy loop() ở lõi 1, nên giao
//     diện phải sang lõi 0.
//
//  3. HAI TÁC VỤ KHÔNG ĐƯỢC CHẠM VÀO ĐỒ CỦA NHAU:
//     PubSubClient không an toàn đa luồng, và IR phải ở lõi 1. Nên tác vụ UI
//     KHÔNG tự publish, KHÔNG tự bắn IR — nó đặt hàng vào queue, loop() rút ra
//     thi hành. Đây đúng là khuôn đã dùng cho callback MQTT trong main.cpp
//     ("callback chỉ bóc gói ra rồi đặt hàng, còn loop() mới phát IR + gửi ack").
//
//  Sở hữu phần cứng, chia dứt khoát:
//     tác vụ UI  -> SPI màn hình, bus I2C (cảm ứng + DS1307), LEDC (đèn nền/còi)
//     loop()     -> WiFi, MQTT, ESP-NOW, IR, NVS
// ============================================================================
namespace Ui {

/// Ảnh chụp trạng thái node để vẽ. loop() dựng lại mỗi vòng rồi gọi publish();
/// tác vụ UI chép ra dưới mutex và tự so với lần vẽ trước để chỉ tô ô nào đổi.
///
/// Mọi số đo dùng NAN cho "không có", KHÔNG dùng 0. App tiền nhiệm từng mặc
/// định thiếu số về 0.0 rồi hiện setpoint bịa — ở đây chặn bằng cùng một luật.
struct Model {
  /// Số ô cảm biến góc phòng mà giao diện dựng sẵn. 8 = AC_BLE_MAX_NODES của
  /// khuôn gói BLE, để không node hợp lệ nào lọt ra ngoài màn. Lắp thật hiện là
  /// 4; các ô thừa đơn giản là không được vẽ (xem `roomSlots`).
  static const uint8_t MAX_ROOMS = 8;

  // --- kết nối ---
  bool     wifiUp   = false;
  int      rssi     = 0;
  bool     mqttUp   = false;
  char     ip[16]   = "";
  char     ssid[33] = "";
  char     mac[18]  = "";
  uint8_t  channel  = 0;

  // --- số đo trong nhà: TRUNG VỊ của các node góc phòng ---
  //
  // KHÔNG PHẢI SỐ CỦA BO NÀY NỮA. Bo gateway không còn cảm biến; hai số dưới đây
  // do RoomRegistry::median() dựng ở lõi 1 rồi chép sang. NAN = chưa góc nào có
  // số đo, và màn phải hiện skeleton chứ không hiện 0.
  float    tIn = NAN, hIn = NAN;

  // --- từng góc phòng (BLE mesh) ---
  //
  // Có mặt ở đây, chứ không chỉ mỗi số trung vị, VÌ ĐÓ LÀ LÝ DO CÓ BỐN CẢM BIẾN:
  // trung vị che đi đúng cái thông tin đáng giá nhất — góc nào đang lệch. Người
  // đứng trước panel cần thấy "góc cửa sổ 29°, ba góc kia 25°" để biết nên kéo
  // rèm, chứ không phải một con 25.5° không giải thích được.
  uint8_t  roomSlots       = 0;      ///< số góc ĐÃ NGHE THẤY — chỉ vẽ ngần này
  uint8_t  roomOnlineCount = 0;      ///< số góc còn nghe thấy (kể cả cảm biến hỏng)
  uint8_t  roomVoting      = 0;      ///< số góc thật sự tham gia trung vị
  float    roomT[MAX_ROOMS]  = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};
  float    roomH[MAX_ROOMS]  = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};
  bool     roomOnline[MAX_ROOMS] = {false};
  uint8_t  roomCorner[MAX_ROOMS] = {0};   ///< nhãn góc node tự khai; 0xFF = không khai
  uint32_t roomAgeSec[MAX_ROOMS] = {0};

  // --- Arduino UNO Q (edge AI, qua Bluetooth) ---
  bool     unoqUp = false;   ///< có đang kết nối GATT không
  uint32_t unoqRx = 0;       ///< số đề xuất/lệnh đã nhận từ nó

  // --- số đo ngoài trời (qua ESP-NOW) ---
  float    tOut = NAN, hOut = NAN;
  bool     outOnline  = false;
  uint32_t outAgeSec  = 0;      // giây kể từ gói ESP-NOW cuối
  uint32_t espnowRx   = 0;
  uint32_t espnowDrop = 0;

  // --- máy lạnh ---
  char     mode[8]  = "";       // "COOL"/"DRY"/"FAN"/"OFF", rỗng = chưa biết
  int      setpoint = -1;       // -1 = chưa biết
  bool     overrideLocal = false;   // ghi đè đặt từ chính màn này (README §8.3)
  uint32_t lastCmdSec = 0;      // giây kể từ lệnh cuối của server

  /// Tổ hợp nào đã có mã IR trong NVS. loop() tính sẵn rồi nhét vào đây thay vì
  /// để tác vụ UI tự hỏi IrStore: NVS do lõi 1 sở hữu, mà một cái bitmask 16
  /// bit chép qua rẻ hơn nhiều so với dựng khoá đồng bộ cho cả kho.
  /// bit i = có mã cho COOL (16 + i), i = 0..14.
  uint16_t coolMask = 0;
  bool     hasDry = false, hasFan = false, hasOff = false;

  // --- học remote ---
  bool     learning = false;
  char     learnLabel[24] = "";
  uint32_t learnRemainSec = 0;

  // --- linh tinh ---
  uint16_t irCodeCount = 0;
  uint32_t uptimeSec   = 0;
  const char *fw       = "";
};

/// Người dùng vừa bấm gì. Tác vụ UI đẩy vào queue, loop() rút ra thi hành.
///
/// DEL_CODE đi CHUNG hàng đợi này chứ không qua SettingFn, dù nút bấm nằm trong
/// màn Cài đặt: SettingFn được tác vụ UI gọi TRỰC TIẾP ở lõi 0 (nó dành cho phần
/// cứng mà lõi 0 sở hữu — đèn nền, còi), còn xoá mã là ghi NVS, mà NVS thuộc
/// quyền loop() ở lõi 1 (xem bảng chia sở hữu ở đầu file). Gọi IrStore từ lõi 0
/// là hai lõi cùng mở một namespace Preferences — hỏng theo kiểu ngẫu nhiên.
struct Command {
  /// RESYNC: xin máy chủ gửi lại toàn bộ kho mã IR. Không mang mode/setpoint.
  enum Kind : uint8_t { MANUAL, AUTO, DEL_CODE, RESYNC } kind;
  char mode[8];
  /// MANUAL: nhiệt độ đặt. DEL_CODE: nhiệt độ của tổ hợp cần xoá, -1 cho mã cố
  /// định (DRY/FAN/OFF) — cùng quy ước với IrStore::removeAlias().
  int  setpoint;
};

/// Một dòng nhật ký: lệnh vừa nhận từ backend và node đã làm gì với nó.
///
/// KHÔNG mang dấu thời gian. Giờ do DS1307 giữ, mà DS1307 nằm trên bus I2C
/// thuộc quyền tác vụ UI (xem bảng chia sở hữu đầu file) — loop() đọc vào đây là
/// hai lõi cùng nói chuyện trên một bus. Nên tác vụ UI tự đóng dấu lúc rút hàng
/// đợi: nó trễ hơn thời điểm gói tới đúng một nhịp vẽ (200ms), không đáng kể so
/// với độ phân giải phút mà màn hiển thị.
struct CmdLog {
  /// Node đã làm gì với lệnh này. Đây là phần mà log serial vốn có còn màn thì
  /// không — và cũng là câu hỏi người lắp thật sự cần trả lời khi máy lạnh không
  /// phản ứng: lệnh KHÔNG tới, hay tới rồi mà node không phát được?
  enum Result : uint8_t {
    SENT,      // có khung IR, đã/đang bắn ra máy lạnh
    NO_CODE,   // backend không gửi ir_raw lẫn ir_code_id — tổ hợp này chưa học
    NEED_RAW,  // có ir_code_id nhưng NVS rỗng -> đang xin server gửi lại
    DUPLICATE, // trùng req_id đã thi hành (broker phát lại QoS1) -> bỏ, chỉ ack
  };

  char    mode[8];
  int     setpoint;
  char    reason[24];   // "auto:COOL@25" / "manual override" / "action:FAN_SPEED"
  Result  result;
};

/// Ghi một dòng nhật ký. Gọi từ loop() (kể cả trong callback MQTT — chỉ đẩy vào
/// hàng đợi, không chạm LVGL và không publish gì).
void logCommand(const CmdLog &e);

/// Dựng màn + cảm ứng + đèn nền + còi, rồi tạo tác vụ UI trên lõi 0.
/// Gọi trong setup(), TRƯỚC connectWifi() để người lắp thấy màn sáng ngay —
/// và để màn còn vẽ được trong lúc WiFi đang dò mạng.
/// Trả false nếu không dò thấy chip cảm ứng (màn vẫn hiện, chỉ không bấm được).
bool begin();

/// loop() gọi mỗi vòng: chép ảnh chụp vào vùng chia sẻ. Rẻ (một memcpy dưới
/// mutex), không vẽ gì cả — việc vẽ là của tác vụ UI.
void publish(const Model &m);

/// loop() gọi mỗi vòng: rút yêu cầu người dùng vừa bấm. Trả false nếu không có.
bool pollCommand(Command &out);

/// loop() gọi sau khi thi hành xong: dòng kết quả hiện thành toast trên màn.
/// nullptr = thành công, dùng lời mặc định.
void reply(const char *msg);

// KHÔNG CÒN readIndoor()/setIndoor(). Bo này từng mang cảm biến của riêng nó
// (DHT22 ở lõi 1, hoặc SHT3x do tác vụ UI đọc trên bus I2C), nên cần một kho
// chung có khoá để hai lõi trao số cho nhau. Nay nguồn duy nhất là các node góc
// phòng, và lõi 1 đã tính sẵn trung vị trước khi gọi publish() — số đi cùng
// Model như mọi trường khác, không cần đường riêng nữa.

} // namespace Ui
