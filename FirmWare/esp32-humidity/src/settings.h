#pragma once
// ============================================================================
//  BreezeLink — node độ ẩm: CHÂN CẮM + THAM SỐ ĐIỀU KHIỂN
// ----------------------------------------------------------------------------
//  FILE NÀY ĐƯỢC COMMIT, không như `config.h` của các bo khác.
//
//  Cố ý: bo này KHÔNG nối WiFi và KHÔNG nối MQTT, nên nó không giữ mật khẩu
//  WiFi, không giữ token MQTT, không giữ DEVICE_UUID nào cả — tức là không có
//  gì để mà giấu. Các bo kia phải gitignore `config.h` vì trong đó có bí mật
//  thật; bắt chước máy móc ở đây chỉ tổ làm mọi giá trị mặc định biến mất khỏi
//  git, và người tiếp theo cắm bo sẽ không biết lấy số ở đâu ra.
//
//  (Tên file cũng cố ý KHÔNG phải `config.h`: FirmWare/.gitignore có mẫu
//  `*/src/config.h`, đặt tên đó là bị loại khỏi repo một cách âm thầm.)
// ============================================================================

// ============================================================================
//  1) CHÂN — ESP32 DevKit V1 (WROOM-32 / D0WD-V3)
// ----------------------------------------------------------------------------
//  KHÔNG BÊ SƠ ĐỒ CHÂN CỦA PANEL SANG. Panel là ESP32-S3 và đặt IR ở IO2/IO3;
//  trên bo này GPIO3 là U0RXD — chân console. Lấy nó làm IR là mất serial, mà
//  serial chính là đường chẩn đoán duy nhất của bo (không màn, không mạng).
//
//  Những chân PHẢI TRÁNH trên ESP32 cổ điển:
//    GPIO0, 2, 5, 12, 15   strapping. GPIO0 là nút BOOT (xem §5, ta DÙNG nó,
//                          nhưng chỉ đọc sau khi đã khởi động xong).
//    GPIO1, GPIO3          UART0 = console qua chip cầu CP210x.
//    GPIO6..11             nối flash SPI trong module. Đụng vào là bo chết ngay.
//    GPIO34..39            CHỈ VÀO, không có trở kéo nội. DHT22 là giao thức
//                          hai chiều nên tuyệt đối không dùng nhóm này.
// ============================================================================

/// DHT22. Cùng chân với node ngoài trời (cũng DevKit V1) cho dễ nhớ khi hàn.
///
/// DHT22 LÀ GIAO THỨC MỘT DÂY HAI CHIỀU — đường dữ liệu phải có trở kéo lên
/// 3.3V. ĐANG DÙNG MODULE 3 CHÂN nên trở 10k đã hàn sẵn trên bo, không phải
/// thêm gì; chỉ khi thay bằng cảm biến 4 chân rời mới phải tự hàn 4.7k–10k.
/// Thiếu trở thì đọc được lúc được lúc không: checksum bắt được nên KHÔNG ra
/// số sai, chỉ ra NaN xen kẽ — triệu chứng giống hệt dây tuột.
///
/// NUÔI DHT22 BẰNG 3.3V, KHÔNG 5V: chân ESP32 không chịu quá áp, mà DHT22 nuôi
/// 5V sẽ lái đường dữ liệu lên gần 5V.
#define DHT_PIN 4

/// LOẠI CẢM BIẾN: 1 = DHT11, 0 = DHT22/AM2302.
///
/// ĐẶT SAI LÀ HỎNG THEO HAI KIỂU KHÁC HẲN NHAU, và phải biết cả hai:
///
///   Cảm biến DHT11 mà khai DHT22 -> khung DHT11 chở số NGUYÊN, giải mã theo
///       DHT22 (16-bit chia 10) cho ra số vô lý cỡ 1536 %RH. Bộ lọc 0..100 ở
///       humidity-sensor.cpp bắt được nên nó báo TRƯỢT — ồn ào, dễ thấy.
///
///   Cảm biến DHT22 mà khai DHT11 -> NGUY HIỂM HƠN NHIỀU: 78,2 %RH biến thành
///       ~3,1 %RH. Vẫn nằm trong khoảng hợp lệ, checksum vẫn đúng, không một
///       dòng lỗi nào. Bo sẽ tin phòng cực khô và cho máy xông chạy mãi.
///
/// Không chắc đang cầm con nào thì cứ đặt rồi xem số: sai kiểu thứ nhất là
/// KHONG CO SO DO liên tục, sai kiểu thứ hai là độ ẩm thấp bất thường (<10%)
/// trong khi phòng rõ ràng không khô như vậy.
#define DHT_SENSOR_IS_DHT11 1

// DHT11 ĐO KÉM HƠN DHT22 KHÁ NHIỀU — nhớ khi đặt ngưỡng ở §2:
//   sai số  ±5 %RH   (DHT22: ±2..5)      -> vùng trễ nên rộng hơn 10 điểm
//   độ phân giải 1 %RH, SỐ NGUYÊN        -> không có phần thập phân
//   dải đo  20..90 %RH                   -> ngoài dải là số không tin được
// Với ±5 %RH thì hai ngưỡng cách nhau 10 điểm là hơi sát: cảm biến lệch 5 về
// một phía lúc bật và 5 về phía kia lúc tắt là ăn hết vùng trễ. Nên để >= 15.

/// LED hồng ngoại PHÁT → máy xông tinh dầu.
#define IR_TX_PIN 14

/// Mắt thu TSOP (nuôi 3.3V) — chỉ bật khi đang HỌC mã, xem ir-remote.cpp.
#define IR_RX_PIN 27

/// Nút BOOT có sẵn trên bo — không phải hàn thêm gì để bật/tắt tay và để học mã.
/// Mức THẤP khi bấm. Giữ nút lúc reset là vào bootloader nạp firmware; điều đó
/// vô hại vì ta chỉ đọc nút sau khi setup() đã chạy xong.
#define BUTTON_PIN 0

/// LED xanh có sẵn trên DevKit V1. Bo này không có màn nên đây là thứ DUY NHẤT
/// nhìn được khi không cắm serial — xem bảng nhấp nháy ở README §5.
#define LED_PIN 2

// ============================================================================
//  2) NGƯỠNG ĐỘ ẨM — hai số, và khoảng cách giữa chúng mới là thứ quan trọng
// ----------------------------------------------------------------------------
//  Cố tình khai HAI ngưỡng rời chứ không phải "đích + sai số": khoảng trống
//  giữa chúng chính là vùng trễ (deadband), và để nó lộ ra thành một con số
//  đọc được thì không ai vô tình đặt nó về 0.
//
//  HAI NGƯỠNG BẰNG NHAU = MÁY BẬT/TẮT LIÊN TỤC quanh điểm cắt. Có kiểm bằng
//  static_assert ở cuối file, không phải chỉ nhắc miệng.
//
//  HAI SỐ NÀY KHÔNG PHẢI TỰ NGHĨ RA — chúng neo vào chính hằng số mà thuật
//  toán comfort của dự án đang dùng, ở src/app/comfort/setpoint_calculator.py:
//
//      HUMID_LOW_KNEE  = 60.0   # dưới 60%RH: KHÔNG phạt, mồ hôi bay hơi tốt
//      HUMID_HIGH_KNEE = 75.0   # trên 75%: phạt dốc hơn
//
//  Tức là backend đã có sẵn quan điểm "60 %RH là mép trên của vùng dễ chịu".
//  Ngưỡng TẮT đặt đúng ở đó: máy xông KHÔNG BAO GIỜ được phép đẩy phòng vượt
//  qua mốc mà chính thuật toán comfort bắt đầu coi là khó chịu — nếu không thì
//  hai bộ điều khiển trong cùng một căn nhà đang đánh nhau, và người ở chỉ
//  thấy "ở trong nhà sao thấy bí bí".
//
//  Ngưỡng BẬT 45 là mốc riêng của việc này: dưới ~40 %RH thì mũi họng khô rõ,
//  chừa 5 điểm để không phải chờ tới lúc đã khó chịu mới bật.
//
//  KHOẢNG TRỄ 15 ĐIỂM cũng vừa đúng thứ DHT11 cần (sai số ±5 %RH, xem §1):
//  lệch 5 về một phía lúc bật rồi lệch 5 về phía kia lúc tắt vẫn còn dư. Bản
//  trước để 45/55 — chỉ 10 điểm, tôi tự đặt trước khi biết sẽ dùng DHT11.
// ============================================================================

/// Khô hơn mức này thì BẬT máy xông (%RH).
#define HUMID_ON_BELOW_RH 45.0f

/// Ẩm hơn mức này thì TẮT (%RH). Ở giữa hai mốc: giữ nguyên trạng thái đang có.
/// = HUMID_LOW_KNEE của setpoint_calculator.py. Đổi số này thì ngó sang đó.
#define HUMID_OFF_ABOVE_RH 60.0f

// ============================================================================
//  3) CHỐNG DAO ĐỘNG — ba lớp, đúng như thuật toán comfort của dự án
// ----------------------------------------------------------------------------
//  Backend chống dao động bằng ba lớp chồng nhau (EMA đầu vào, deadband, dwell)
//  — xem src/app/comfort/mode_decision.py. Bo này lặp lại đủ cả ba:
//      EMA      -> EMA_ALPHA dưới đây
//      deadband -> khoảng trống giữa hai ngưỡng ở §2
//      dwell    -> DWELL_SEC dưới đây
//
//  Bỏ bất kỳ lớp nào thì vẫn "chạy được" trong lúc thử, và chỉ hỏng khi độ ẩm
//  đi ngang đúng mép ngưỡng — tức là ở đúng điều kiện thường gặp nhất.
// ============================================================================

/// Nhịp đọc cảm biến (ms). DHT22 KHÔNG đọc nhanh hơn 2 giây được (giới hạn của
/// chính con cảm biến); 5 giây cho nó thở và giảm tự sinh nhiệt.
#define SAMPLE_MS 5000UL

/// Hệ số làm mượt EMA. 0.2 ở nhịp 5 giây cho hằng số thời gian ~25 giây — đủ
/// nuốt nhiễu một mẫu mà vẫn bám kịp khi mở cửa phòng.
#define EMA_ALPHA 0.2f

/// Tối thiểu bao lâu giữ một trạng thái trước khi được đổi (giây).
///
/// Máy xông tinh dầu không có block máy nén để mà bảo vệ như điều hoà, nhưng
/// vẫn cần dwell vì lý do khác: hơi nước cần vài phút mới lan tới được cảm
/// biến. Không có dwell thì bo bật máy, đo lại sau 5 giây, thấy vẫn khô, và
/// không bao giờ học được rằng lệnh trước ĐÃ có tác dụng.
#define DWELL_SEC 300UL

// ============================================================================
//  4) AN TOÀN — bình nước cạn là ca hỏng thật, không phải giả thuyết
// ============================================================================

/// Chạy liên tục quá lâu thì cắt (giây). Phòng quá khô hoặc cửa mở suốt thì
/// vòng lặp trên KHÔNG BAO GIỜ đạt ngưỡng tắt, và máy sẽ chạy tới cạn bình.
#define MAX_RUN_SEC (4UL * 3600UL)

/// Sau khi bị cắt vì chạy quá lâu thì khoá bấy lâu (giây) trước khi tự động
/// được bật lại.
///
/// PHẢI CÓ, KHÔNG BỎ ĐƯỢC: cắt xong mà không khoá thì vòng lặp kế tiếp vẫn
/// thấy phòng khô và bật lại ngay — tức là MAX_RUN_SEC không có tác dụng gì
/// ngoài việc chèn thêm một lần nháy máy. Nửa tiếng là đủ để người ở nhà nhận
/// ra và đi xem bình nước.
#define REFILL_LOCKOUT_SEC (30UL * 60UL)

/// Mất số đo lâu hơn mức này thì cắt máy (giây).
///
/// Chạy mù còn tệ hơn không chạy: không ai biết phòng đã đủ ẩm chưa, và nhánh
/// MAX_RUN ở trên là thứ duy nhất còn chặn. Với nhịp 5 giây thì 120 giây cho
/// phép trượt 24 lần đọc liên tiếp — quá đủ để phân biệt nhiễu với tuột dây.
#define SENSOR_STALE_SEC 120UL

// ============================================================================
//  5) GHI ĐÈ THỦ CÔNG — bấm nút BOOT là bật/tắt tay
// ----------------------------------------------------------------------------
//  Mượn đúng ngữ nghĩa TỰ ĐỘNG / GHI ĐÈ của panel treo tường: người dùng luôn
//  thắng máy, nhưng KHÔNG thắng vĩnh viễn.
//
//  Vì sao ghi đè phải tự hết hạn: một lần bấm tay mà giữ mãi thì ba tháng sau
//  không còn ai nhớ vì sao máy không tự chạy nữa, và triệu chứng đọc y hệt
//  "firmware hỏng". Tự trả về TỰ ĐỘNG thì cùng lắm mất hai tiếng.
// ============================================================================

/// Giữ trạng thái tay bao lâu rồi tự trả về TỰ ĐỘNG (giây).
#define OVERRIDE_HOLD_SEC (2UL * 3600UL)

/// Giữ nút bao lâu thì tính là nhấn GIỮ = vào chế độ học mã (ms).
#define BUTTON_LONG_MS 3000UL

/// Chờ người bấm remote bao lâu trong chế độ học (ms).
#define LEARN_TIMEOUT_MS 20000UL

// ============================================================================
//  6) REMOTE CỦA MÁY XÔNG — mã BẬT và mã TẮT có RỜI NHAU không?
// ----------------------------------------------------------------------------
//  REMOTE ĐANG DÙNG (đã xem ảnh) có 6 nút:
//
//      [ON/OFF]  [Intermittent]  [Continuous]
//      [timing]  [Big/small]     [Light]
//
//  Chỉ có MỘT nút nguồn duy nhất -> nó là nút BẬP BÊNH. Nên mặc định để 1.
//
//  1 = bo chỉ học một mã, và mỗi lần muốn đổi trạng thái thì bắn đúng mã đó.
//
//      HẠN CHẾ KHÔNG KHẮC PHỤC ĐƯỢC BẰNG PHẦN MỀM: bo không có cách nào biết
//      máy đang bật hay tắt — nó chỉ NHỚ lần cuối nó bắn gì. Ai đó cầm remote
//      bấm tay, hoặc rút điện máy xông rồi cắm lại, là niềm tin đó sai và mọi
//      lệnh sau đều NGƯỢC. Trạng thái có lưu vào NVS để sống qua lần reset của
//      CHÍNH BO NÀY, nhưng đó là tất cả những gì làm được. Chữa bằng cách bấm
//      nút BOOT một lần để đồng bộ lại.
//
//  0 = mã BẬT và mã TẮT là HAI khung KHÁC NHAU. Tốt hơn hẳn: bắn lại cũng vô
//      hại, nên niềm tin sai TỰ SỬA ở lần quyết định kế tiếp.
//
//  >>> ĐÁNG THỬ 2 PHÚT TRƯỚC KHI CHẤP NHẬN SỐ 1 <<<
//
//  Remote này có nút [Continuous] rời. Ở RẤT NHIỀU máy xông, bấm một nút CHẾ ĐỘ
//  trong lúc máy đang tắt sẽ BẬT máy luôn vào chế độ đó — tức là ta có sẵn một
//  mã "BẬT" thật sự, không bập bênh.
//
//      Phép thử: TẮT máy xông. Bấm [Continuous]. Máy có phun sương không?
//
//        CÓ    -> đặt về 0, rồi học:  o BAT = nut [Continuous]
//                                     o TAT = nut [ON/OFF]
//                 Thoát hẳn được vụ lệch pha. Đây là cấu hình nên dùng.
//
//        KHÔNG -> giữ 1, học o BAT = nut [ON/OFF]. Đành chịu hạn chế ở trên.
//
//  KHÔNG THỬ THÌ ĐỂ 1. Đoán nhầm theo chiều này chỉ làm máy lệch pha một lần;
//  đoán nhầm theo chiều kia (khai 0 trong khi cả hai ô đều học nút [ON/OFF])
//  thì mã "TẮT" thực ra cũng là nút bập bênh, và bo sẽ BẬT máy đúng vào lúc nó
//  tưởng mình đang tắt.
//
//  BA NÚT CÒN LẠI KHÔNG DÙNG, và cố ý không hỗ trợ:
//    [timing]    máy tự tắt theo giờ của RIÊNG NÓ -> bo mất dấu trạng thái,
//                đúng cái hỏng mà MAX_RUN_SEC ở §4 sinh ra để thay thế.
//    [Big/small] lượng sương. Chỉnh tay một lần là xong, không cần tự động.
//    [Light]     đèn trang trí, không liên quan độ ẩm.
#define DIFFUSER_IR_TOGGLE 1

// ============================================================================
//  Kiểm lúc biên dịch — thà đứt build còn hơn nháy máy ngoài hiện trường
// ============================================================================
static_assert(HUMID_OFF_ABOVE_RH > HUMID_ON_BELOW_RH,
              "Nguong TAT phai LON HON nguong BAT — bang nhau hoac nguoc lai la "
              "mat hoan toan vung tre, may se bat/tat lien tuc quanh diem cat.");
static_assert(DWELL_SEC * 1000UL > SAMPLE_MS,
              "DWELL_SEC phai dai hon mot nhip doc, khong thi dwell vo tac dung.");
static_assert(MAX_RUN_SEC > DWELL_SEC,
              "MAX_RUN_SEC ngan hon DWELL_SEC thi may bi cat truoc khi kip doi "
              "trang thai lan nao.");
static_assert(EMA_ALPHA > 0.0f && EMA_ALPHA <= 1.0f,
              "EMA_ALPHA phai trong khoang (0, 1].");
