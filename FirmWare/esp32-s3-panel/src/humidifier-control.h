#pragma once
#include <Arduino.h>

// ============================================================================
//  Máy tạo độ ẩm: quyết định BẬT / TẮT, chạy ngay trên panel.
// ----------------------------------------------------------------------------
//  NGUỒN GỐC: bản port của bo thử `esp32-humidity` (đã gỡ khỏi repo ở lần dọn
//  15/08/2026 — bo đó chỉ dùng để thử logic trước khi nạp lên bo thật, và giờ
//  bản chạy thật là chính file này). Muốn xem bản gốc thì:
//      git log --diff-filter=D -- FirmWare/esp32-humidity/src/diffuser-control.cpp
//
//  Giữ nguyên sáu nhánh ưu tiên, ba lớp chống dao động, và nguyên tắc "nghi ngờ
//  thì TẮT". BA CHỖ ĐÃ ĐỔI so với bản gốc, cả ba do panel có thứ bo kia không có:
//
//   1. ĐỘ ẨM LÀ TRUNG VỊ BỐN GÓC PHÒNG, không phải một con DHT gắn trên bo.
//      Bo thử đo bằng cảm biến của chính nó nên phải tắt WiFi để khỏi tự sinh
//      nhiệt làm sai số đo. Panel không có cảm biến nào cả — số vào đây do
//      RoomRegistry::median() dựng từ các node góc phòng, cách xa panel — nên
//      ràng buộc đó biến mất.
//
//   2. MÃ IR HỌC TỪ APP, nằm trong kho chung của panel dưới hai bí danh
//      "HUMID_ON" / "HUMID_OFF" (xem ir_action_service.KNOWN_ACTIONS). Không có
//      kho hai-ô riêng nữa.
//
//   3. REMOTE BẬP BÊNH TỰ NHẬN RA, không còn cờ biên dịch DIFFUSER_IR_TOGGLE.
//      Hộ nào remote chỉ có một nút nguồn thì học cùng một khung vào cả hai ô,
//      hoặc chỉ học ô BẬT — Emitter bên main.cpp lo phần đó. Module này không
//      cần biết, và đó là điểm đáng giá: một cờ biên dịch đặt sai chỉ lộ ra
//      ngoài hiện trường, còn ở đây thì "chưa học ô TẮT" là một trạng thái đọc
//      được ngay trên màn.
//
//  THỨ TỰ ƯU TIÊN (trên đè dưới) — phần dễ viết sai nhất, giữ nguyên bản gốc:
//     1. Chạy quá lâu      -> CẮT, và cắt cả ghi đè tay
//     2. Ghi đè tay        -> giữ nguyên ý người dùng
//     3. Mất số đo         -> CẮT
//     4. Đang khoá đổ nước -> giữ TẮT
//     5. Trễ (deadband)    -> muốn gì
//     6. Dwell             -> có được đổi bây giờ không — CHỈ CHẶN CHIỀU TẮT,
//                             xem DWELL_SEC cho lý do bất đối xứng
// ============================================================================
namespace HumidifierControl {

// --- Ngưỡng và thời gian ------------------------------------------------------
//  Chép nguyên số (kể cả lý do) từ bo thử trước khi nó bị gỡ khỏi repo. Đây là
//  bản duy nhất còn lại, nên sửa ở đây là sửa cho cả sản phẩm — không còn bản
//  thứ hai để mà lệch.
//
//  KHÔNG ĐƯA VÀO config.h: file đó bị gitignore vì chứa mật khẩu, nên mọi giá
//  trị mặc định đặt trong đó sẽ biến mất khỏi repo và người tiếp theo không biết
//  lấy số ở đâu ra.

/// Khô hơn mức này thì BẬT (%RH).
constexpr float ON_BELOW_RH = 45.0f;

/// Ẩm hơn mức này thì TẮT (%RH). Ở giữa: giữ nguyên.
///
/// = HUMID_LOW_KNEE của src/app/comfort/setpoint_calculator.py. Neo vào đó có
/// chủ đích: máy tạo ẩm KHÔNG BAO GIỜ được đẩy phòng vượt qua mốc mà chính
/// thuật toán comfort bắt đầu coi là khó chịu — nếu không thì hai bộ điều khiển
/// trong một căn nhà đang đánh nhau, và người ở chỉ thấy "sao thấy bí bí".
constexpr float OFF_ABOVE_RH = 60.0f;

/// Nhịp gọi update() (ms). EMA_ALPHA dưới đây tính theo đúng nhịp này.
constexpr uint32_t TICK_MS = 5000UL;

/// Làm mượt đầu vào. 0.5 ở nhịp 5 giây -> hằng số thời gian ~7 giây.
///
/// NÂNG TỪ 0.2 (τ ~22 giây) VÌ MÁY BẬT QUÁ CHẬM sau khi phòng khô. Với bước
/// 50%->40%, ngưỡng 45 nằm đúng giữa nên số đã lọc cần `ln0.5/ln(1-α)` nhịp để
/// cắt qua: ở 0.2 là ~16 giây, ở 0.5 là **một nhịp = 5 giây**.
///
/// AN TOÀN VÌ ĐẦU VÀO ĐÃ ĐƯỢC LỌC MỘT LẦN RỒI: số vào đây là TRUNG VỊ bốn góc
/// phòng, tức đã khử nhiễu theo KHÔNG GIAN. EMA chỉ còn phải khử nhiễu theo THỜI
/// GIAN của chính con trung vị đó, và một con trung vị thì khó nhảy đột ngột.
/// Lớp chống dao động thật sự vẫn là DEADBAND 15 điểm ở §2, không phải EMA.
constexpr float EMA_ALPHA = 0.5f;

/// Tối thiểu bao lâu giữ trạng thái trước khi được TẮT (giây).
///
/// CHỈ ÁP CHO CHIỀU TẮT — bật thì đi ngay, không chờ. Bất đối xứng có chủ đích,
/// cùng khuôn với "giành lái chậm, nhả lái nhanh" của lớp edge AI:
///
///   BẬT MUỘN  -> phòng cứ khô, người ở cảm nhận được ngay. Đây đúng là lỗi
///                đã bị báo: dwell 300 giây chặn cả lần bật đầu tiên.
///   TẮT MUỘN  -> máy phun thêm vài phút. Gần như vô hại.
///
/// Và lý do gốc của dwell chỉ đúng cho chiều TẮT: "hơi nước cần vài phút mới lan
/// tới cảm biến" là chuyện xảy ra SAU KHI ĐÃ BẬT — nó ngăn ta vội kết luận lệnh
/// vừa rồi không có tác dụng. Nó không nói gì về việc có nên bật hay không khi
/// phòng đang khô thật.
///
/// KHÔNG SỢ DAO ĐỘNG khi bỏ dwell chiều bật: đã bật rồi thì phải vượt 60%RH mới
/// tắt (deadband 15 điểm), nên không có đường nào để bật/tắt liên tục.
constexpr uint32_t DWELL_SEC = 300UL;

/// Chưa học mã IR thì thử lại mỗi bấy nhiêu giây (giây).
///
/// Tách khỏi DWELL_SEC vì dwell nay không còn chặn chiều bật: không có bộ đếm
/// riêng thì mỗi nhịp 5 giây lại thử bắn một lần và log phun ra liên tục cho tới
/// khi có người vào app học mã.
constexpr uint32_t NO_CODE_RETRY_SEC = 300UL;

/// Chạy liên tục quá lâu thì cắt (giây). Phòng quá khô hoặc cửa mở suốt thì
/// vòng lặp không bao giờ đạt ngưỡng tắt, và máy chạy tới cạn bình.
constexpr uint32_t MAX_RUN_SEC = 4UL * 3600UL;

/// Khoá sau lần cắt trên (giây). BỎ ĐI LÀ MẤT LUÔN TÁC DỤNG CỦA MAX_RUN_SEC:
/// cắt xong mà không khoá thì vòng kế tiếp vẫn thấy phòng khô và bật lại ngay.
constexpr uint32_t REFILL_LOCKOUT_SEC = 30UL * 60UL;

/// Mất số đo lâu hơn mức này thì cắt (giây). Chạy mù tệ hơn không chạy.
constexpr uint32_t SENSOR_STALE_SEC = 120UL;

/// Ghi đè tay tự hết hạn sau bấy lâu (giây). Cùng ngữ nghĩa TỰ ĐỘNG/GHI ĐÈ của
/// máy lạnh: người dùng luôn thắng máy, nhưng KHÔNG thắng vĩnh viễn — một lần
/// bấm giữ mãi thì ba tháng sau không ai nhớ vì sao máy thôi tự chạy.
constexpr uint32_t OVERRIDE_HOLD_SEC = 2UL * 3600UL;

static_assert(OFF_ABOVE_RH > ON_BELOW_RH,
              "Nguong TAT phai LON HON nguong BAT — bang nhau la mat hoan toan "
              "vung tre, may se bat/tat lien tuc quanh diem cat.");
static_assert(DWELL_SEC * 1000UL > TICK_MS,
              "DWELL_SEC phai dai hon mot nhip, khong thi dwell vo tac dung.");
static_assert(MAX_RUN_SEC > DWELL_SEC, "MAX_RUN_SEC phai dai hon DWELL_SEC.");

/// Vì sao trạng thái hiện tại lại như vậy. Có mặt để màn trả lời được câu "sao
/// máy không chạy?" bằng MỘT dòng — không có nó thì người dùng phải tự suy từ
/// mấy con số rời rạc, và họ sẽ suy sai.
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
  float    rh;                ///< độ ẩm ĐÃ LÀM MƯỢT, NAN khi chưa có số đo
  uint32_t stateAgeSec;       ///< giữ trạng thái hiện tại bao lâu rồi
  uint32_t overrideLeftSec;   ///< còn bao lâu tự về TỰ ĐỘNG (0 = không ghi đè)
  uint32_t lockoutLeftSec;    ///< còn bao lâu hết khoá đổ nước (0 = không khoá)
  uint32_t dwellLeftSec;      ///< còn bao lâu mới ĐƯỢC PHÉP đổi (0 = đổi được ngay)
};

/// Bên gọi cung cấp cách bắn IR.
///
/// TRẢ FALSE KHI KHÔNG BẮN ĐƯỢC (chưa học mã). Bộ điều khiển sẽ KHÔNG đổi niềm
/// tin trạng thái trong ca đó — vì máy thật có đổi gì đâu. Đây là chỗ rất dễ
/// viết sai: coi như đã đổi rồi thì panel tin máy đang chạy, sẽ không thử bật
/// lại nữa, và phòng cứ khô mãi trong khi log nói "da bat".
typedef bool (*Emitter)(bool on);

/// Gọi một lần trong setup(), SAU IrStore::begin().
void begin(Emitter emit);

/// Một lượt đo + quyết định. [rhRaw] là độ ẩm THÔ (trung vị các góc); NAN = chưa
/// có số đo.
///
/// PHẢI GỌI ĐÚNG NHỊP TICK_MS — hàm này CỐ Ý không tự chặn nhịp. EMA_ALPHA gắn
/// chặt với nhịp gọi (0.2 ở 5 giây = hằng số thời gian ~25 giây), nên tự chặn
/// bên trong sẽ che mất chuyện người gọi đang gọi sai nhịp: bộ lọc lặng lẽ đổi
/// hằng số thời gian mà không có triệu chứng nào. Để người gọi giữ nhịp thì cái
/// nhịp đó nằm ngay chỗ đọc được trong loop().
void tick(float rhRaw, uint32_t nowMs);

/// Bật/tắt bằng tay — vào GHI ĐÈ, tự hết hạn sau OVERRIDE_HOLD_SEC.
/// CÓ dọn luôn khoá đổ nước: "vừa đổ nước xong, chạy lại đi" là ca dùng chính.
void manualSet(bool on, uint32_t nowMs);

/// Thoát GHI ĐÈ, trả về TỰ ĐỘNG ngay lập tức.
void backToAuto(uint32_t nowMs);

Status status(uint32_t nowMs);

/// Câu mô tả ngắn của [r] — tiếng Việt CÓ DẤU (hiện lên màn LVGL, font ui/fonts
/// có đủ dải). Log serial thì dùng reasonAscii() — xem chú thích ở .cpp.
const char *reasonText(Reason r);

}  // namespace HumidifierControl
