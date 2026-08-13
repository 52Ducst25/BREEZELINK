#pragma once
#include <Arduino.h>

#include "espnow-message.h"
#include "slave-watch.h"   // SLAVE_TIMEOUT_MS — xem ROOM_STALE_MS bên dưới

// ============================================================================
//  Bảng các node cảm biến góc phòng mà gateway đang nghe được.
// ----------------------------------------------------------------------------
//  Gateway KHÔNG CÒN CẢM BIẾN CỦA RIÊNG NÓ. Con số "trong nhà" hiện trên màn,
//  con số gửi sang Arduino UNO Q, và (qua backend) con số thuật toán comfort
//  dùng — tất cả đều đến từ đây: trung vị của các góc còn tươi.
//
//  KHOÁ THEO device_uuid, KHÔNG theo số góc: uuid là định danh thật, đến sẵn
//  trong mỗi gói ESP-NOW, và nó là thứ backend cũng dùng. `corner` chỉ là nhãn
//  hiển thị, nên hai bo trùng nhãn vẫn được đếm là hai node — khác hẳn nếu khoá
//  theo nhãn, nơi trùng số nghĩa là mất hẳn số đo của một góc mà không ai biết.
//
//  VÌ SAO TRUNG VỊ CHỨ KHÔNG PHẢI TRUNG BÌNH CỘNG:
//  bốn cảm biến nằm bốn góc, và thế nào cũng có một góc dính nắng cửa sổ hoặc
//  nằm ngay dưới miệng gió điều hoà. Góc đó lệch 3-4°C so với phòng, mà trung
//  bình cộng thì để nó kéo nhiệt độ đặt đi theo — vĩnh viễn, không triệu chứng
//  nào ngoài "ở trong nhà thấy sai sai". Trung vị bỏ qua hẳn một điểm lạc miễn
//  là ba góc kia đồng ý.
//
//  ĐÂY LÀ BẢN SONG SINH CỦA src/app/comfort/room_aggregate.py Ở PHÍA BACKEND, và
//  hai bên PHẢI cho cùng một con số — nếu không thì màn treo tường và app trong
//  tay người dùng nói hai nhiệt độ khác nhau về cùng một phòng, và không bên nào
//  sai rõ ràng để mà sửa. Đổi luật ở một bên thì đổi cả hai.
// ============================================================================
namespace RoomRegistry {

/// Bao nhiêu góc theo dõi cùng lúc. Bằng số ô SlaveWatch theo dõi được, trừ một
/// chỗ cho node ngoài trời.
static const uint8_t MAX_ROOMS = 6;

/// Bao lâu không nghe thấy thì coi là mất kết nối (ms).
///
/// LẤY THẲNG NGƯỠNG CỦA SlaveWatch, không đặt số riêng: chính SlaveWatch là bên
/// publish "offline" lên topic status của node phòng, nên hai ngưỡng lệch nhau
/// nghĩa là màn treo tường và web nói hai chuyện khác nhau về cùng một góc —
/// đúng loại mâu thuẫn mà không ai chịu tin bên nào.
static const uint32_t ROOM_STALE_MS = SlaveWatch::SLAVE_TIMEOUT_MS;

struct Room {
  bool     used;
  char     uuid[33];
  uint8_t  corner;      ///< nhãn hiển thị; AC_CORNER_NONE nếu node không khai
  float    t, h;        ///< NAN = node còn sống nhưng cảm biến hỏng
  uint32_t lastHeardMs;
};

/// Ghi nhận một gói vừa nghe được từ node góc phòng. Bên gọi đã lọc node_kind.
/// Trả false nếu bảng đầy (node thứ 7 trở đi không được theo dõi).
bool update(const AcEspNowPacket &pkt);

/// Trung vị nhiệt độ/độ ẩm của các góc CÒN TƯƠI và CÓ SỐ ĐO.
/// Trả false khi không góc nào đủ điều kiện — bên gọi phải hiểu là "chưa biết
/// nhiệt độ trong nhà" và KHÔNG được thay bằng 0.0 hay số cũ.
/// [usedOut] nhận số góc đã tham gia (bỏ qua nếu nullptr).
bool median(float &tempC, float &humidity, uint8_t *usedOut = nullptr);

/// Số góc đang còn tươi (kể cả góc sống mà cảm biến hỏng).
uint8_t onlineCount();

/// Số ô đã từng nghe thấy — dùng để biết đã lắp mấy góc mà không phải khai
/// trước trong config.h.
uint8_t knownCount();

/// Ô thứ [index] theo thứ tự nghe thấy lần đầu. nullptr nếu vượt knownCount().
const Room *at(uint8_t index);

/// Giây kể từ lần cuối nghe thấy ô này; 0 nếu chưa từng nghe.
uint32_t ageSec(uint8_t index);

/// Ô này còn tươi không.
bool online(uint8_t index);

// Việc BÁO online/offline lên cloud KHÔNG nằm ở đây: SlaveWatch đã làm đúng việc
// đó cho node ngoài trời, khoá theo device_uuid, và node phòng dùng lại y nguyên.
// Viết thêm một cơ chế theo dõi thứ hai là tạo ra hai nguồn sự thật cho cùng một
// câu hỏi "node này còn sống không".

} // namespace RoomRegistry
