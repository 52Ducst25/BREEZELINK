#pragma once
#include <Arduino.h>

// ============================================================================
//  Theo dõi node SLAVE còn sống hay không (chạy trên node MASTER).
// ----------------------------------------------------------------------------
//  VÌ SAO CẦN: node slave không nối MQTT nên broker KHÔNG có Last Will cho nó.
//  Slave mất điện/hỏng thì không ai báo, web sẽ hiện "Trực tuyến" mãi mãi —
//  tệ hơn cả không hiện gì, vì nó khẳng định sai.
//
//  CÁCH LÀM: mỗi gói ESP-NOW của slave chính là một nhịp tim. Master ghi lại
//  thời điểm nghe được lần cuối; quá SLAVE_TIMEOUT_MS không nghe thấy thì master
//  ĐỨNG TÊN slave publish "offline" lên topic status của nó, nghe lại thì
//  publish "online".
// ============================================================================
namespace SlaveWatch {

/// Slave bắn nhịp tim mỗi 3s; ngưỡng 20s -> chịu được ~6.6 nhịp rơi LIÊN TIẾP
/// mới báo mất kết nối. Đây là cấu hình ưu tiên ỔN ĐỊNH, chống nhấp nháy.
///
/// Cách đạt an toàn cao nhất là giữ nhịp tim DÀY nhưng ngưỡng RỘNG, chứ không
/// phải kéo dài cả hai: nhịp dày cho nhiều cơ hội "điểm danh" trong cùng một
/// cửa sổ, nên một chuỗi nhiễu sóng cũng không đủ làm node bị coi là chết.
/// Đổi lại: mất điện thật thì mất ~20 giây mới hiện offline — đã cân nhắc và
/// chấp nhận, vì báo nhầm liên tục làm người dùng mất tin vào đèn trạng thái,
/// tệ hơn nhiều so với biết chậm vài giây.
static const uint32_t SLAVE_TIMEOUT_MS = 20000UL;

/// Nhịp tim 5s là để biết SỐNG/CHẾT nhanh, không phải để lưu số đo dày đặc:
/// nhiệt độ phòng không đổi trong 5 giây, lưu hết chỉ làm phồng DB và bắt thuật
/// toán comfort tính lại vô ích. Nên số đo chỉ được đẩy lên cloud mỗi 15s.
static const uint32_t RELAY_INTERVAL_MS = 15000UL;

/// Số slave tối đa theo dõi cùng lúc (1 hộ hiện có 1; chừa chỗ cho nhiều phòng).
static const uint8_t MAX_SLAVES = 8;

typedef void (*StatusChanged)(const char *deviceUuid, bool online);

/// Ghi nhận vừa nghe thấy slave. Nếu đây là lần đầu hoặc nó vừa sống lại,
/// [cb] được gọi với online=true (để bên gọi publish status).
void heard(const char *deviceUuid, StatusChanged cb);

/// Định kỳ khẳng định LẠI slave đang online, không chỉ báo lúc chuyển trạng thái.
/// Lý do: khi slave chuyển từ chế độ WiFi sang ESP-NOW, broker phải chờ hết
/// keepalive (~22s) mới nhận ra phiên MQTT cũ đã chết rồi mới bắn Last Will
/// "offline" — cái này ĐÈ LÊN thông báo "online" master vừa gửi trước đó. Nếu
/// chỉ báo một lần thì node sống nhăn mà web hiện offline vĩnh viễn.
static const uint32_t STATUS_REFRESH_MS = 60000UL;

/// true nếu đã tới lúc đẩy số đo của slave này lên cloud (>= RELAY_INTERVAL_MS
/// kể từ lần đẩy trước). Gọi sau heard(); trả true thì tự tính là đã đẩy.
bool dueForRelay(const char *deviceUuid);

/// true nếu đã tới lúc khẳng định lại trạng thái online của slave này.
bool dueForStatusRefresh(const char *deviceUuid);

/// Gọi mỗi vòng loop(): slave nào quá hạn thì [cb] được gọi với online=false.
void checkTimeouts(StatusChanged cb);

} // namespace SlaveWatch
