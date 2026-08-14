#pragma once
#include <Arduino.h>

// ============================================================================
//  Quyết định BẬT / TẮT máy xông tinh dầu.
// ----------------------------------------------------------------------------
//  Đây là bản song sinh thu nhỏ của src/app/comfort/mode_decision.py ở backend:
//  cùng ba lớp chống dao động (EMA đầu vào + deadband + dwell), cùng thứ tự ưu
//  tiên "muốn gì" rồi mới "có được phép đổi không".
//
//  KHÁC MỘT CHỖ CỐT LÕI, và nó quyết định cả cấu trúc file này: điều hoà thì
//  server luôn biết máy đang ở chế độ nào vì chính server ra lệnh. Ở đây, với
//  remote một-nút-bập-bênh, bo KHÔNG ĐO ĐƯỢC trạng thái máy — nó chỉ có niềm
//  tin. Nên mọi nhánh dưới đây được viết theo nguyên tắc: **nghi ngờ thì TẮT**.
//  Máy xông tắt oan thì phòng khô thêm vài phút; máy xông bật oan mà không ai
//  biết thì nó chạy tới cạn bình.
//
//  THỨ TỰ ƯU TIÊN (trên đè dưới) — thứ tự này là phần dễ viết sai nhất:
//     1. Chạy quá lâu      -> CẮT, và cắt cả ghi đè tay
//     2. Ghi đè tay        -> giữ nguyên ý người dùng
//     3. Mất số đo         -> CẮT
//     4. Đang khoá đổ nước -> giữ TẮT
//     5. Trễ (deadband)    -> muốn gì
//     6. Dwell             -> có được đổi bây giờ không
// ============================================================================
namespace DiffuserControl {

/// Vì sao trạng thái hiện tại lại như vậy. Có mặt để `status` trên serial trả
/// lời được câu "sao máy không chạy?" bằng MỘT dòng — không có nó thì người
/// dùng phải tự suy từ mấy con số rời rạc, và họ sẽ suy sai.
enum class Reason : uint8_t {
  BOOT,         ///< vừa khởi động, chưa quyết định lần nào
  AUTO_DRY,     ///< tự động: phòng khô hơn ngưỡng BẬT
  AUTO_WET,     ///< tự động: phòng ẩm hơn ngưỡng TẮT
  DEADBAND,     ///< nằm giữa hai ngưỡng — giữ nguyên, đúng như thiết kế
  DWELL_HOLD,   ///< muốn đổi rồi nhưng chưa đủ thời gian giữ tối thiểu
  MANUAL,       ///< người dùng đang ghi đè
  MAX_RUN,      ///< bị cắt vì chạy liên tục quá lâu
  LOCKOUT,      ///< đang khoá chờ đổ nước sau lần cắt trên
  SENSOR_LOST,  ///< mất số đo quá lâu
  NO_CODE,      ///< muốn đổi nhưng CHƯA HỌC MÃ IR cho việc đó
};

struct Status {
  bool     on;                ///< niềm tin: máy đang chạy?
  bool     overriding;        ///< đang ở chế độ GHI ĐÈ tay?
  Reason   reason;
  uint32_t stateAgeSec;       ///< giữ trạng thái hiện tại bao lâu rồi
  uint32_t overrideLeftSec;   ///< còn bao lâu thì tự về TỰ ĐỘNG (0 nếu không)
  uint32_t lockoutLeftSec;    ///< còn bao lâu hết khoá đổ nước (0 nếu không)

  /// Còn bao lâu nữa mới ĐƯỢC PHÉP đổi trạng thái (0 = đổi được ngay).
  ///
  /// Có mặt vì lý do rất cụ thể: khi lý do là DWELL_HOLD thì câu hỏi tiếp theo
  /// của người nhìn LUÔN là "chờ bao lâu nữa?" — mà trước đó họ phải tự lấy
  /// DWELL_SEC trừ đi `stateAgeSec`, tức phải biết DWELL_SEC bằng bao nhiêu,
  /// tức phải đi mở settings.h. Một màn hình bắt người ta mở mã nguồn để đọc
  /// nó thì chưa làm xong việc.
  uint32_t dwellLeftSec;
};

/// Bên gọi cung cấp cách bắn IR.
///
/// TRẢ FALSE KHI KHÔNG BẮN ĐƯỢC (chưa học mã). Bộ điều khiển sẽ KHÔNG đổi niềm
/// tin trạng thái trong trường hợp đó — vì máy thật có đổi gì đâu. Đây là chỗ
/// rất dễ viết sai: coi như đã đổi rồi thì bo tin máy đang chạy, sẽ không thử
/// bật lại nữa, và phòng cứ khô mãi trong khi log nói "da bat".
typedef bool (*Emitter)(bool on);

void begin(Emitter emit);

/// Gọi mỗi vòng loop().
/// [humidityRh] là số ĐÃ LÀM MƯỢT; [sensorOk] = false khi chưa có số đo hợp lệ.
void update(float humidityRh, bool sensorOk, uint32_t nowMs);

/// Bật/tắt bằng tay — vào chế độ GHI ĐÈ, tự hết hạn sau OVERRIDE_HOLD_SEC.
/// Ghi đè tay CÓ bỏ qua khoá đổ nước (người dùng vừa đổ nước xong là ca chính).
void manualSet(bool on, uint32_t nowMs);

/// Đảo trạng thái hiện tại và vào GHI ĐÈ. Đây là việc nút BOOT làm.
void manualToggle(uint32_t nowMs);

/// Thoát GHI ĐÈ, trả về TỰ ĐỘNG ngay lập tức.
void backToAuto(uint32_t nowMs);

Status status(uint32_t nowMs);

/// Câu mô tả ngắn của [r], tiếng Việt không dấu (đi ra serial).
const char *reasonText(Reason r);

}  // namespace DiffuserControl
