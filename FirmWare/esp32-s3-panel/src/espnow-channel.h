#pragma once
#include <Arduino.h>

// ============================================================================
//  Giữ panel ở ĐÚNG KÊNH mà các node đang phát — kể cả khi mất WiFi.
// ----------------------------------------------------------------------------
//  VÌ SAO CẦN. ESP-NOW bắt buộc hai bên cùng kênh. Node góc phòng và node ngoài
//  trời tự quét SSID của router rồi bám kênh đó (shared/espnow-slave-radio.h),
//  nên ROUTER là ĐIỂM HẸN của cả hệ. Panel thì trước đây không có một dòng nào
//  chọn kênh: nó ăn theo kênh mà giao diện station đang bám.
//
//  Hệ quả: mất WiFi là mất luôn điểm hẹn. Tệ hơn, vòng thử nối lại gọi
//  `WiFi.begin()` trần mỗi 15 giây, mà `begin()` không biết kênh thì station
//  QUÉT KHẮP CÁC KÊNH để tìm SSID — radio panel không bao giờ đậu yên, và gói
//  của node rơi vào đúng lúc nó đang ở kênh khác thì mất. Broadcast không có ACK
//  nên node vẫn báo gửi thành công, không một dòng lỗi nào ở bất kỳ đâu.
//
//  CÁCH LÀM — SOI GƯƠNG HÀNH VI CỦA NODE, KHÔNG SÁNG TẠO THÊM.
//  Node không sửa được (4 bo trên tường + 1 bo ngoài trời, KHÔNG CÓ OTA), nên
//  panel phải là bên khớp theo. Hành vi thật của node, đọc từ
//  espnow-slave-radio.h:
//
//      lúc boot   : quét 3 lần -> thấy: bám kênh router · trượt: bám kênh 1
//      mỗi 5 phút : quét      -> thấy: bám kênh router · trượt: GIỮ NGUYÊN kênh
//
//  Chú ý dòng cuối: node KHÔNG BAO GIỜ rơi về kênh 1 khi router chết giữa chừng,
//  nó bám lì kênh cuối cùng biết được. Nên panel cũng phải nhớ kênh cuối cùng và
//  quay về đúng đó — chứ KHÔNG phải rơi về kênh 1. Đây là chỗ dễ làm sai nhất:
//  "cả hai cùng fallback kênh 1" nghe hợp lý nhưng sẽ đẩy panel sang kênh 1
//  trong khi node vẫn nằm ở kênh cũ, tức là tự tay tạo ra đúng cái lệch kênh
//  đang muốn chữa.
//
//  KÊNH PHẢI SỐNG QUA LẦN KHỞI ĐỘNG LẠI, nên nó nằm trong NVS: mất điện cả nhà
//  rồi có điện lại nhưng router hỏng là ca có thật, và khi đó panel vừa boot
//  không có gì trong RAM để mà nhớ.
// ============================================================================
namespace EspNowChannel {

/// Kênh dùng khi CHƯA TỪNG thấy router (bo mới nạp, NVS trống).
///
/// PHẢI LÀ 1, và không được đổi thành số khác cho "đẹp": đây đúng là kênh mà
/// EspNowSlaveRadio::begin() rơi về khi quét trượt 3 lần lúc boot. Hai bên cùng
/// lạc thì vẫn phải lạc về cùng một chỗ mới gặp được nhau.
static const uint8_t FALLBACK_CHANNEL = 1;

/// Chu kỳ dò lại kênh router trong lúc đang mất WiFi (ms).
///
/// BẰNG ĐÚNG EspNowSlaveRadio::RESCAN_INTERVAL_MS của node. Không phải trùng hợp
/// và cũng không nên "tối ưu" lệch đi: dò thưa hơn node thì có quãng panel còn
/// bám kênh cũ trong khi node đã chuyển; dò dày hơn thì tốn thêm những lần quét
/// mà mỗi lần đều kéo radio ra khỏi việc thu ~1 giây.
static const uint32_t RESCAN_INTERVAL_MS = 300000UL;

/// Nạp kênh đã nhớ từ NVS. Gọi trong setup(), TRƯỚC connectWifi().
void begin();

/// Ghi nhận kênh router vừa thấy (gọi mỗi lần WiFi nối thành công).
/// Chỉ chạm NVS khi số thật sự đổi — flash có hạn ghi.
void note(uint8_t channel);

/// Kênh đã nhớ, hoặc FALLBACK_CHANNEL nếu chưa từng thấy router.
uint8_t last();

/// Ghim radio về kênh đã nhớ. Trả false nếu phần cứng từ chối.
///
/// CHỈ GỌI KHI ĐÃ MẤT WiFi, và PHẢI gọi sau `WiFi.disconnect(false)`: lúc station
/// còn đang dò mạng thì ngăn xếp WiFi tự lái kênh theo ý nó và lệnh đặt kênh ở
/// đây bị ghi đè trong im lặng. Gọi lúc ĐANG nối được vào router thì còn tệ hơn —
/// nó cắt luôn đường WiFi mà không ai yêu cầu.
bool park();

/// Thôi ghim (WiFi đã nối lại — từ giờ router giữ kênh hộ).
void release();

/// Radio còn ở đúng kênh ghim không; lệch thì kéo về. Gọi mỗi vòng loop() TRONG
/// LÚC ĐANG ĐẬU (không gọi khi đang dở một lần thử nối, xem dưới).
///
/// RẺ: chỉ đọc thanh ghi kênh, và chỉ đặt lại khi thật sự lệch — nên gọi mỗi
/// vòng không tốn gì.
///
/// VÌ SAO CẦN, dù park() đã đọc ngược để xác nhận: park() chỉ đúng TẠI THỜI ĐIỂM
/// gọi. Ngăn xếp WiFi có thể lái kênh về sau vì những việc không phải mình khởi
/// xướng (một sự kiện nội bộ, một lần dò sót lại). Không có hàm này thì lần sửa
/// gần nhất là ở lượt thử kế tiếp — tức là tối đa 5 phút câm, mà không có gì báo.
///
/// KHÔNG GỌI KHI ĐANG THỬ NỐI: lúc đó ngăn xếp WiFi đang cố tình nhảy kênh để dò
/// SSID, kéo nó về là hai bên giằng nhau và không lần nối nào xong.
void hold();

/// Panel có đang tự ghim kênh hay không. Dùng cho màn THÔNG TIN: người đi lắp
/// cần phân biệt "kênh này do router quyết" với "kênh này do panel tự nhớ" —
/// hai trạng thái đó dẫn tới hai chỗ phải đi tìm khác hẳn nhau.
bool pinned();

/// Quét tìm [ssid] để cập nhật kênh, rồi BÁM LẠI. Trả true nếu thấy router.
///
/// LUÔN BÁM LẠI KỂ CẢ KHI KÊNH KHÔNG ĐỔI, kể cả khi quét trượt. scanNetworks()
/// nhảy qua tất cả các kênh và bỏ radio lại ở kênh cuối cùng nó dừng, KHÔNG tự
/// trả về chỗ cũ. Đây đúng là cái bẫy đã làm node "chết ngầm" một lần và được
/// ghi lại ở EspNowSlaveRadio::tickRescan — đừng để panel dẫm lại.
///
/// TỐN ~1 GIÂY và trong lúc đó không thu được gói nào, nên gọi thưa (xem
/// RESCAN_INTERVAL_MS) và tuyệt đối không gọi khi WiFi đang nối bình thường.
bool rescan(const char *ssid);

} // namespace EspNowChannel
