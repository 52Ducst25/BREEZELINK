# Kiến trúc hệ thống

Cập nhật: 2026-08-11 · Phạm vi: toàn hệ, sau khi chuyển từ 2 node sang 6 thiết bị mỗi hộ.

Tài liệu này trả lời **vì sao** từng ranh giới nằm ở chỗ nó đang nằm. Cách chạy và cách
nạp firmware nằm ở [`../README.md`](../README.md); chi tiết từng phase nằm trong
`plans/260811-1809-kien-truc-4-node-phong-espnow-uno-q/`.

---

## 1. Sáu thiết bị trong một hộ

| Thiết bị | `node_type` | Đường về | Có cảm biến? | Nối MQTT? |
|---|---|---|---|---|
| 4× ESP32-C3-DevKitM-1 | `room` | ESP-NOW → gateway | DHT22 | không |
| 1× QR Box Advance (WROOM-32) | `indoor` | WiFi + MQTT | **không** | có (master) |
| 1× ESP32 DevKit V1 | `outdoor` | ESP-NOW → gateway | DHT22 | không |
| 1× Arduino UNO Q | — | **Bluetooth GATT** → gateway | không | **không** |

**Chỉ gateway có phiên MQTT.** Mọi thứ khác — bốn góc phòng, node ngoài trời, và cả
UNO Q — đều câm với cloud. Bốn node cảm biến được gateway đứng tên publish hộ, nên chúng
không cần credential riêng, không cần WiFi của khách, và đổi mật khẩu WiFi không làm
chúng chết. UNO Q thì cố ý không có đường lên cloud nào cả: xem §5.

---

## 2. Ba quyết định định hình toàn bộ phần còn lại

### 2.1 Nhiệt độ "trong nhà" là TRUNG VỊ của nhiều cảm biến

Một cảm biến treo tường đo được **cái tường đó**, không phải căn phòng. Bốn góc chênh
3–4 °C là bình thường.

**Trung vị chứ không trung bình cộng:** trung bình cho phép một góc lạc (nắng cửa sổ,
miệng gió) kéo nhiệt độ đặt đi vĩnh viễn, và triệu chứng duy nhất là "ở trong nhà thấy
sai sai" — không log, không cảnh báo, không cách nào truy. Trung vị bỏ qua hẳn một điểm
lạc miễn là ba góc còn lại đồng ý.

Quy tắc này tồn tại ở **ba nơi** và cả ba **phải cho cùng một số**:

| Nơi | File |
|---|---|
| Backend (nguồn chân lý) | `src/app/comfort/room_aggregate.py` |
| Gateway (màn tại chỗ + gói gửi UNO Q) | `FirmWare/esp32-s3-panel/src/room-registry.cpp` |
| Edge AI | import lại chính file backend, qua `edge-ai/edge_ai/comfort_bridge.py` |

Bản C++ là bản sao **bắt buộc phải có** (firmware không import được Python) — đổi luật ở
một bên thì đổi cả hai, nếu không màn treo tường và app nói hai nhiệt độ khác nhau về cùng
một phòng và không bên nào sai rõ ràng để mà sửa.

**Và có một cái chốt cho đúng việc đó:** edge AI tự tính lại trung vị bằng bản Python rồi
so với con số gateway gửi sang. Lệch quá 0.05 °C là nó ghi một dòng WARNING nêu đích danh
hai file đã trôi khỏi nhau (`controller._MEDIAN_DRIFT_C`). Không có chốt này thì kiểu lệch
đó không có triệu chứng nào cả.

### 2.2 Comfort engine KHÔNG biết có bao nhiêu cảm biến

`comfort_engine.compute()` vẫn nhận đúng một cặp `(tin, hin)` như thời hai node. Việc gộp
xảy ra **trước** nó, trong `telemetry_handler`, rồi ghi vào `state:indoor` — cùng cái khoá
Redis mà bản cũ dùng.

Nhờ vậy chuyển từ 1 lên 4 cảm biến **không sửa một dòng nào** trong `comfort/` ngoài việc
thêm một module thuần hàm mới. Thuật toán là phần rủi ro nhất của dự án (audit §1: chưa có
test, thiết bị đã ở nhà khách) — giữ nó bất động là có chủ đích.

### 2.3 Đích của lệnh điều khiển là một LOOKUP, không phải suy luận

Trước đây "node nào nhận lệnh IR" suy ra được từ node vừa gửi số đo, vì chỉ có một node
trong nhà. Nay phần lớn telemetry đến từ node **không có phần cứng IR**.

`telemetry_service.get_gateway_device()` là nơi duy nhất trả lời câu đó: `role=master`
trước (và **từ chối** node `room`), rồi mới rơi về `node_type=indoor` cũ nhất. Chọn nhầm
là lệnh publish thành công, không ai thi hành, và không có nack nào — node đó thậm chí
không subscribe.

---

## 3. Ba đường vô tuyến, một ăng-ten

Gateway chạy đồng thời WiFi (MQTT), ESP-NOW và BLE trên một khối radio 2.4 GHz. Bộ đồng
tồn tại của IDF chia thời gian, và thứ tự ưu tiên được cài vào thiết kế:

- **Gateway KHÔNG quét BLE.** Nó chỉ quảng bá và giữ **một** kết nối GATT với UNO Q —
  quét mới là thứ ăn sóng liên tục. Vai trò NimBLE bị cắt xuống còn peripheral +
  broadcaster ngay ở `platformio.ini`.
- **Số đo cảm biến đi ESP-NOW**, vốn dùng chung radio WiFi sẵn có chứ không mở thêm đường.
- **MQTT được nhường trước** — nó là đường **duy nhất** để lệnh máy lạnh đi xuống.

### Vì sao mọi cảm biến đi ESP-NOW

Gói ESP-NOW chở 250 byte nên mỗi node mang thẳng `device_uuid` 32 ký tự của chính nó. Hệ
quả là **gateway không giữ bảng tra nào**: thêm hay bớt một góc chỉ cần nạp bo mới.

Gói BLE advertising cổ điển chỉ có 31 byte — chở không nổi uuid. Đi đường đó thì gateway
buộc phải giữ một mảng `uuid[]` theo thứ tự và nạp lại mỗi lần đổi node; lệch một ô là số
đo của góc A nộp lên cloud dưới tên góc B, biểu đồ vẫn có số và không lỗi ở đâu cả.

Cái giá của ESP-NOW: mọi bên **bắt buộc cùng kênh WiFi**, nên node phải quét tìm SSID của
nhà để biết router đang ở kênh nào. Đó là cái bẫy hỏng-câm số một của cả hệ — xem §7.

### Vì sao Bluetooth chỉ dành cho UNO Q

Đường này **hai chiều** (UNO Q phải ra lệnh ngược) và cần chở một ảnh chụp cả bốn góc.
Advertising không làm được cả hai; GATT thì thương lượng MTU lên hàng trăm byte và có kênh
ghi ngược sẵn.

Vai: **gateway = peripheral, UNO Q = central**. UNO Q chạy Debian + BlueZ, một central đầy
đủ và dễ lập trình; ESP32 làm peripheral là khuôn mẫu nhẹ nhất cho nó. Và nếu đảo vai thì
gateway phải đi *quét* mỗi lần UNO Q khởi động lại — đúng thứ vừa nói là phải tránh.

### Trần 39 byte và cái bẫy MTU

Ảnh chụp là 39 byte, nhưng **MTU mặc định của BLE là 23** (tức 20 byte dữ liệu). Notify
vượt MTU bị **cắt cụt trong im lặng** — không lỗi ở cả hai bên, chỉ là mấy góc cuối biến
mất. Nên: gateway xin MTU 247 ngay khi khởi tạo, kiểm lại lúc kết nối và in cảnh báo to
nếu hụt; phía Python kiểm độ dài trước khi giải mã và nói thẳng "gần như chắc chắn MTU quá
nhỏ nên gói bị cắt cụt".

Kích thước gói được chốt bằng `static_assert` bên C và `assert` lúc import bên Python.
Thêm một trường ở một bên mà quên bên kia thì gói vẫn "giải mã thành công", chỉ là mọi
trường sau chỗ chèn đều lệch — CRC không cứu được, vì nó tính trên đúng số byte mà bên gửi
*nghĩ* là đúng.

---

## 4. Luồng dữ liệu một chu kỳ

```
4 node góc phòng ─NOW─┐
                      │
node ngoài trời ──NOW─┴─► gateway ─MQTT──► telemetry_handler
                             ▲ │
              Bluetooth GATT │ ▼
                          Arduino UNO Q (edge AI)
                                                │
                             ┌──────────────────┼──────────────────┐
                        node_type=room     =outdoor            =indoor
                             │                  │             (firmware cũ)
                    set_room_state()     set_outdoor_state()       │
                             │                  │                  │
                    aggregate_rooms()           │                  │
                        (trung vị)              │                  │
                             └────────► state:indoor ◄─────────────┘
                                                │
                                       comfort_engine.compute()
                                                │
                                    get_gateway_device()  ← lookup, không suy luận
                                                │
                                        command_publisher ─MQTT─► gateway ─IR─► máy lạnh
```

Chỉ tick của node **outdoor** được phép đẩy `tout_ema` (`is_outdoor_tick`). Bốn node phòng
tick dày gấp bội; để chúng đẩy EMA thì nhiệt độ đặt sẽ bám theo chính hơi lạnh máy đang
thổi ra thay vì bám thời tiết.

---

## 5. Edge AI: ai cầm lái

Cloud và UNO Q cùng ra lệnh là máy lạnh nhận hai lệnh trái nhau — và triệu chứng (nhiệt độ
đặt tự nhảy) trông y hệt lỗi thuật toán, đẩy người truy lỗi sang đúng nửa sai của hệ thống.
Nên luật **bất đối xứng có chủ đích**:

| | điều kiện | vì sao |
|---|---|---|
| Giành lái | gateway báo cloud im ≥ 300 s (20 nhịp telemetry) | giành muộn chỉ mất vài phút không thích ứng |
| Nhả lái | **ngay** nhịp ảnh chụp đầu tiên báo cloud đã lên tiếng | nhả muộn là hai bên giành máy nén |

**Đề xuất ≠ lệnh.** Mọi gói UNO Q gửi đều mang `kind`, và gateway **chỉ bắn hồng ngoại khi
nhận `COMMAND`**. Bình thường nó gửi `ADVICE` — gateway ghi vào nhật ký trên màn và không
làm gì. Gộp hai cái nghĩa là mọi phép thử trên UNO Q đều chạy thẳng vào máy nén.

**Ai đếm sự im lặng.** Gateway, không phải UNO Q — nó giữ phiên MQTT nên biết chắc chắn
hơn, và nó chỉ đếm lệnh của **máy chủ**. Điều đó xoá sạch một lớp lỗi mà bản MQTT trước đó
có: dịch vụ subscribe đúng topic nó publish, nên lệnh giành lái của chính nó vọng về, bị
đọc thành "cloud sống lại", và nó nhả lái một nhịp sau khi giành — mãi mãi, 30 giây một lần.

**Lệnh của UNO Q không đi qua đường ghi đè.** Gateway có sẵn `runPanelCommand()` làm gần
đúng việc cần, nhưng đường đó đặt cờ ghi đè và xin máy chủ mở cổng override. Ghi đè là để
*người dùng* giành quyền **khỏi** máy chủ; UNO Q thì đang **đứng thay** máy chủ. Đi nhầm
đường đó thì lúc mạng về, máy chủ bị khoá ngoài suốt `override_hours` bởi chính lớp dự
phòng vừa cứu nó — và màn hiện "GHI ĐÈ" trong khi không ai bấm gì.

**Vì sao BLE chứ không MQTT.** Lớp dự phòng phải sống sót đúng cái sự cố nó sinh ra để
chịu đựng. Đi qua broker nghĩa là khi mất mạng — đúng lúc cần nó nhất — nó cũng mất luôn
đường tới gateway. BLE là liên kết trực tiếp giữa hai thiết bị cùng phòng.

Edge **import** thuật toán từ `src/app/comfort/` chứ không chép: hai bản sẽ lệch dần theo
mỗi lần sửa backend, và hậu quả của việc lệch là một cái máy lạnh chạy sai nhiệt độ trong
nhà người ta. Ngoại lệ duy nhất là 11 hằng số cấu hình (import chúng kéo theo cả SQLAlchemy).

---

## 6. Định danh và bảo mật ở tầng thiết bị

| Lớp | Cơ chế | Chống được gì | KHÔNG chống được gì |
|---|---|---|---|
| ESP-NOW | `magic`/`version` + uuid tự khai | gói rác khác hệ trên cùng kênh | thiết bị trong tầm sóng tự xưng uuid |
| BLE (UNO Q) | `link_key` = FNV-1a(ORG_ID) + CRC8 + chống lặp `seq` | UNO Q của hộ khác trong chung cư; đồ chơi BLE ghi bừa | kẻ cố ý — khoá nằm trong config.h và đi trần trên sóng |
| MQTT | user/pass mỗi device | thiết bị không có credential | publish sang topic hộ khác (cần ACL broker — audit §6) |
| Backend | `get_device_for_topic` khớp org | **gõ nhầm ORG_ID** trong `config.h` | kẻ cố ý có credential hợp lệ |

Siết BLE thật khi cần: bật bonding + passkey tĩnh của NimBLE rồi ghép đôi một lần lúc
lắp. Chưa làm vì nó thêm một bước lắp đặt có thể sai, và mối đe doạ ở đây (ai đó trong
tầm 10 m muốn chỉnh máy lạnh nhà bạn) không tương xứng.

Lưu ý ngược lại: node cảm biến **không** có lớp lọc nào theo danh sách trắng, vì gói tự
khai uuid. Một bo lạ tự xưng uuid hợp lệ sẽ được trung chuyển — nhưng backend từ chối
uuid không có trong `devices`, nên nó chỉ tốn một dòng log.

---

## 7. Những chỗ hỏng CÂM (không có lỗi nào được in ra)

Danh sách này là thứ đáng đọc nhất trong tài liệu — mỗi mục đều đã có chốt chặn trong mã,
và chú thích tại chỗ giải thích chốt đó.

| Triệu chứng | Nguyên nhân | Chốt chặn |
|---|---|---|
| Một góc không bao giờ lên | `WIFI_SSID` gõ sai / là băng 5 GHz -> node bám nhầm kênh | node in tên mạng nó đang tìm lúc boot; broadcast không có ACK nên đây là chốt duy nhất |
| Node ngoài trời im sau khi nâng cấp gói | gateway chỉ nhận v2, bo cũ vẫn gửi v1 | `acEspNowParse()` nhận cả hai, v1 hiểu là outdoor |
| Ảnh chụp tới UNO Q thiếu mấy góc cuối | MTU < 42, notify bị cắt cụt | gateway kiểm MTU lúc kết nối + Python kiểm độ dài trước khi giải mã |
| Trường trong ảnh chụp lệch chỗ, nhiệt độ ra số rác | thêm trường một bên, quên bên kia | `static_assert` bên C + `assert` lúc import bên Python |
| Máy chủ bị khoá ngoài 2 giờ sau khi mạng về | lệnh edge đi qua đường ghi đè của panel | `runUnoQIncoming()` có đường thi hành riêng |
| Cùng một lệnh edge thi hành hai lần | UNO Q ghi lại sau khi kết nối lại | chống lặp theo `seq` trong `unoq-link.cpp` |
| Màn tường và app nói hai nhiệt độ khác nhau | `room-registry.cpp` và `room_aggregate.py` trôi khỏi nhau | edge kiểm chéo trung vị, WARNING nếu lệch > 0.05 °C |
| MAC gateway hiện "—" trên trang nạp firmware | gateway hết publish telemetry | MAC đi kèm gói `state` |
| Vòng comfort đứng im, log chỉ nói "indoor state unknown" | gateway mất DHT22 mà backend chưa biết `room` | phase 01 — nhánh `room` trong `telemetry_handler` |

---

## 8. Còn nợ (theo audit `plans/reports/audit-260729-2217-*.md`)

Chưa đóng: không có test tự động, không CI, firmware không có OTA, MQTT chạy plaintext
1883, EMQX ACL cấp tay, telemetry không có retention. Kiến trúc mới **làm nặng thêm** mục
retention: một hộ nay có 6 nguồn ghi thay vì 2.
