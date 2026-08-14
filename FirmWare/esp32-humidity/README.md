# Node độ ẩm — tự bật máy xông tinh dầu

Đo độ ẩm phòng, thấy khô thì bật máy xông tinh dầu bằng hồng ngoại, đủ ẩm thì tắt.
Bấm nút bật/tắt tay lúc nào cũng được. **Không nối mạng** — chạy độc lập hoàn toàn.

| | |
|---|---|
| Chip | **ESP32-D0WD-V3** rev 3.1 (ESP32 cổ điển, Xtensa 2 lõi) — *đã đọc bằng esptool, không phải suy đoán* |
| Bo | ESP32 DevKit V1 / WROOM-32, 4 MB flash |
| Cầu USB | **CP210x** → `Serial` đi qua UART0 |
| Cảm biến | **DHT11** (tạm) — đổi `DHT_SENSOR_IS_DHT11` trong `src/settings.h` khi thay DHT22 |
| Nạp | `pio run -e esp32-humidity -t upload --upload-port COM7` |

```bash
pio run -e esp32-humidity -t upload --upload-port COM7   # nạp
pio device monitor -p COM7 -b 115200                     # bảng điều khiển
```

---

## 1. Đấu dây

**Chín dây, ba module** — mỗi module 3 dây: nguồn / tín hiệu / mass.

| Module | Chân module | → ESP32 |
|---|---|---|
| **DHT11** | `+` / VCC | `3V3` |
| | `out` / DATA / S | **`GPIO4`** |
| | `−` / GND | `GND` |
| **IR phát** | `+` / VCC | `3V3` |
| | `S` (tín hiệu) | **`GPIO14`** |
| | `−` / GND | `GND` |
| **IR thu** (KY-022) | `VCC` | `3V3` |
| | `DAT` | **`GPIO27`** |
| | `GND` | `GND` |

```
        ESP32 DevKit V1                        Module
       ┌───────────────┐                    ┌────────────────┐
       │           3V3 ├────────────────────┤ +   / VCC      │
       │         GPIO4 ├────────────────────┤ out / DATA / S │  DHT11
       │           GND ├────────────────────┤ −   / GND      │
       │               │                    └────────────────┘
       │           3V3 ├────────────────────┤ +   / VCC      │
       │        GPIO14 ├────────────────────┤ S              │  IR PHÁT
       │           GND ├────────────────────┤ −   / GND      │
       │               │                    └────────────────┘
       │           3V3 ├────────────────────┤ VCC            │
       │        GPIO27 ├────────────────────┤ DAT            │  IR THU
       │           GND ├────────────────────┤ GND            │
       │               │                    └────────────────┘
       │  [BOOT] GPIO0 │  có sẵn trên bo — không hàn
       │  [LED]  GPIO2 │  có sẵn trên bo — không hàn
       └───────────────┘
```

### Nuôi 3.3V — nhưng lý do khác nhau ở hai module IR

| Module | 5V được không |
|---|---|
| **IR thu** (`DAT` → GPIO27) | **KHÔNG.** `DAT` là ngõ RA, nuôi 5V thì nó nhả 5V vào GPIO (ngưỡng tuyệt đối 3.6V) |
| **IR phát** (`S` ← GPIO14) | **Được.** `S` là ngõ VÀO, ESP32 vẫn lái 3.3V. Chỉ có lợi nếu module **có transistor** (con SOT-23 đen 3 chân cạnh LED); loại chỉ có LED + trở thì chân VCC không nối đâu cả, nuôi 5V vô ích |
| **DHT11** | Không cần — 3.3V là đủ và an toàn |

Trang bán ghi "5V" chỉ là thông số chung của module, không phải bắt buộc. TSOP38238 bên
trong chạy 2.5–5.5V.

**Module DHT11 3 chân có sẵn trở kéo 10k**, không hàn thêm gì. Chỉ khi dùng cảm biến
**4 chân rời** mới phải tự hàn trở 4.7k–10k giữa DATA và 3.3V (chân 3 bỏ trống).

**Mắt thu TSOP có 3 chân và rất dễ cắm ngược.** Cắm ngược thì nó không chạy mà cũng
không hỏng — tức là không có triệu chứng nào ngoài việc học mã mãi không được.

### Vì sao không dùng IO2/IO3 như panel

Panel là ESP32-**S3** và đặt IR ở IO2/IO3. Bo này là ESP32 cổ điển, ở đó **GPIO3 là
U0RXD** — chân console. Bê nguyên sơ đồ chân của panel sang là mất serial, mà serial
chính là đường chẩn đoán duy nhất của bo (không màn, không mạng).

Chân đã tránh: `GPIO1/3` (UART0), `GPIO6..11` (flash SPI, đụng là chết bo),
`GPIO34..39` (chỉ vào — DHT22 hai chiều nên không dùng được), `GPIO5/12/15` (strapping).

---

## 2. Dùng

### Nút BOOT — hai cử chỉ

| Cử chỉ | Việc |
|---|---|
| **Nhấn nhả nhanh** | bật/tắt tay → vào **GHI ĐÈ**, tự hết hạn sau 2 giờ |
| **Giữ ≥ 3 giây** | vào chế độ **HỌC MÃ** (LED nháy nhanh = thả tay ra được) |

### Học mã — chĩa remote thật vào mắt thu

Không phải nạp lại firmware, và đổi sang máy xông hãng khác cũng không.

1. **Tắt bớt đèn phòng** — xem cảnh báo dưới, đây không phải chi tiết vặt
2. Giữ nút BOOT 3 giây (hoặc gõ `learn` trên serial)
3. Chĩa remote máy xông vào mắt thu, **cách ~2cm**
4. Bấm nút `[ON/OFF]`
5. Đọc dòng kết quả — **nhìn TÊN GIAO THỨC, đừng chỉ nhìn số mốc**

```
=== DA HOC o BAT: 71 moc, giao thuc NEC (32 bit) ===
    Giao thuc co ten -> gan nhu chac chan la remote that, khong phai nhieu.
```

### ⚠ Đèn phòng đẻ ra mã giả, và nó trông y hệt mã thật

Đèn LED/huỳnh quang phát hồng ngoại liên tục. Mắt thu nghe thấy, và thỉnh thoảng nhiễu
lọt ra một khung dài vừa đủ để **lọt bộ lọc và được cất vào NVS**. Log báo "đã học", số
mốc trông hợp lý, mọi thứ có vẻ xong — rồi máy xông không bao giờ nhúc nhích, và **không
có một dòng lỗi nào** để mà đi tìm.

Chuyện này đã xảy ra thật trong lúc dựng bo:

| | Học lúc đèn sáng, cách 5cm | Học lúc tắt đèn, cách 2cm |
|---|---|---|
| Số mốc | 60 | **71** |
| Giao thức | `UNKNOWN` | **`NEC` (32 bit)** |
| Khung nhiễu kèm theo | hàng chục khung 1023 mốc | **0** |
| Máy xông phản ứng | ❌ không | ✅ có |

**Số mốc không phân biệt được remote với rác. Tên giao thức thì có** — nhiễu gần như không
bao giờ giải mã ra một giao thức có tên. Ra `UNKNOWN` thì `wipe` rồi học lại; học hai lần
liên tiếp mà đều ra cùng một giao thức thì mới chắc.

(`UNKNOWN` **không** chắc chắn là hỏng — bo phát lại nguyên văn nên remote dùng giao thức
lạ vẫn chạy. Nó chỉ là lý do để nghi ngờ.)

Mã nằm trong NVS nên **sống qua mất điện và qua cả `pio run -t upload`**.
Chỉ `esptool erase_flash` mới xoá mất.

### Lệnh serial (115200)

```
status | s     bang trang thai (Enter khong go gi cung duoc)
on / off       bat/tat TAY (vao GHI DE, tu het han)
auto           bo GHI DE, tra ve TU DONG ngay
learn          hoc ma cho o con trong
learn on|off   hoc ma cho dung mot o
forget on|off  quen ma mot o
test on|off    ban thu khung, KHONG doi trang thai (de ngam LED)
burst          ban lap 15 phat / 2s de chinh huong va do tam
diag           do chan mat thu: day co toi GPIO27 khong (khoi doan)
blink          nhay LED phat 5s de soi bang camera dien thoai
wipe           xoa sach ma IR + niem tin trang thai
```

### Ba lệnh chẩn đoán — và vì sao chúng đáng có

"Học mãi không được" có **năm nguyên nhân mà cả năm cho đúng một triệu chứng: im lặng**
(mất nguồn · sai chân · cắm ngược VCC/GND · mắt thu hỏng · hết pin remote). Không đo được
thì chỉ còn cách tháo ra cắm lại từng thứ, mỗi vòng vài phút. Ba lệnh này biến nó thành ba
câu hỏi trả lời được:

| Lệnh | Trả lời | Đọc kết quả |
|---|---|---|
| `diag` | dây mắt thu có tới GPIO27 không | Kéo chân **xuống** bằng trở nội mà vẫn đọc ra **CAO 100%** → có thứ đang chủ động lái lên → mắt thu có nguồn và nối đúng. Ra THẤP → dây chưa tới. **Không cần bấm remote**, nên tách được "dây hỏng" với "chưa ai bấm gì" |
| `blink` | LED phát có được lái không | Nháy 10Hz trong 5s, soi bằng **camera điện thoại** (mắt thường không thấy hồng ngoại). Cố ý nháy chậm chứ không bắn khung thật — khung thật chỉ vài chục ms, camera 30fps bắt không kịp và sẽ kết luận nhầm là LED chết |
| `learn` | bắt được là remote hay **rác** | Xem mục dưới |

---

## 3. Thuật toán

Lấy đúng khuôn chống dao động **ba lớp** của thuật toán comfort trong dự án
(`src/app/comfort/mode_decision.py`):

| Lớp | Ở đây là |
|---|---|
| EMA đầu vào | `EMA_ALPHA = 0.2` ở nhịp 5 giây → hằng số thời gian ~25 giây |
| Deadband | khoảng trống giữa **BẬT dưới 45%** và **TẮT trên 55%** |
| Dwell | `DWELL_SEC = 300` — tối thiểu 5 phút mới được đổi trạng thái |

Bỏ lớp nào cũng "vẫn chạy" lúc thử, và chỉ hỏng khi độ ẩm đi ngang đúng mép ngưỡng —
tức là ở đúng điều kiện thường gặp nhất.

Dwell ở đây không để bảo vệ block máy nén như điều hoà, mà vì **hơi nước cần vài phút
mới lan tới cảm biến**. Không có nó thì bo bật máy, đo lại sau 5 giây, thấy vẫn khô, và
không bao giờ học được rằng lệnh trước đã có tác dụng.

### Thứ tự ưu tiên khi quyết định

Trên đè dưới — đây là phần dễ viết sai nhất:

1. **Chạy quá lâu** (>4h) → cắt, và cắt cả ghi đè tay, rồi **khoá 30 phút**
2. **Ghi đè tay** → giữ nguyên ý người dùng
3. **Mất số đo** (>2 phút) → cắt
4. **Đang khoá đổ nước** → giữ tắt
5. **Trễ** → muốn bật hay tắt
6. **Dwell** → có được đổi bây giờ không

Nhánh 1 nằm trên nhánh 2 có lý do: "bấm tay rồi quên" chính là ca mà giới hạn giờ chạy
sinh ra để chặn. Và **khoá 30 phút là bắt buộc, không bỏ được** — cắt xong mà không khoá
thì vòng lặp kế tiếp vẫn thấy phòng khô và bật lại ngay, tức giới hạn giờ chạy không có
tác dụng gì ngoài việc chèn thêm một lần nháy máy.

Nguyên tắc xuyên suốt: **nghi ngờ thì TẮT**. Máy xông tắt oan thì phòng khô thêm vài
phút; máy xông bật oan mà không ai biết thì nó chạy tới cạn bình.

---

## 4. ⚠ Remote — làm phép thử 2 phút này trước đã

Remote đang dùng có 6 nút:

```
  [ON/OFF]   [Intermittent]   [Continuous]
  [timing]   [Big/small]      [Light]
```

Chỉ có **một** nút nguồn → nó là nút **bập bênh**, nên `settings.h` đang để
`DIFFUSER_IR_TOGGLE = 1`. Nhưng đó là cấu hình *kém hơn*, và có thể thoát được:

> **Phép thử:** tắt máy xông, rồi bấm **`[Continuous]`**. Máy có phun sương không?

| Kết quả | Làm gì |
|---|---|
| **Có** | Đặt `DIFFUSER_IR_TOGGLE = 0`, học `ô BẬT = [Continuous]`, `ô TẮT = [ON/OFF]`. **Nên dùng cách này.** |
| **Không** | Giữ `= 1`, học `ô BẬT = [ON/OFF]`. Chịu hạn chế dưới đây. |

Rất nhiều máy xông bật luôn khi nhận một nút **chế độ** lúc đang tắt — nếu máy bạn vậy
thì mã BẬT và mã TẮT rời nhau, bắn lại vô hại, và **niềm tin sai tự sửa** ở lần quyết
định kế tiếp.

### Nếu buộc phải dùng nút bập bênh (`= 1`)

**Bo không có cách nào biết máy đang bật hay tắt** — nó chỉ *nhớ* lần cuối nó bắn gì.
Niềm tin đó lưu vào NVS nên sống qua lần reset của **chính bo này**, nhưng:

- ai đó cầm remote bấm tay → niềm tin sai
- rút điện máy xông rồi cắm lại → niềm tin sai
- gõ `test on` → máy thật đảo trạng thái, bo thì không

Sai rồi thì **mọi lệnh sau đều ngược**. Chữa: bấm nút BOOT một lần (hoặc gõ `on`/`off`).

**Không thử thì để `1`.** Đoán nhầm theo chiều này chỉ lệch pha một lần; đoán nhầm theo
chiều kia (khai `0` trong khi cả hai ô đều học nút `[ON/OFF]`) thì mã "TẮT" thực ra cũng
là nút bập bênh, và bo sẽ **bật** máy đúng lúc nó tưởng mình đang tắt.

### Ba nút còn lại: cố ý không hỗ trợ

| Nút | Vì sao bỏ |
|---|---|
| `[timing]` | máy tự tắt theo giờ của **riêng nó** → bo mất dấu trạng thái. Đúng cái hỏng mà `MAX_RUN_SEC` sinh ra để thay thế |
| `[Big/small]` | lượng sương — chỉnh tay một lần là xong |
| `[Light]` | đèn trang trí, không liên quan độ ẩm |

---

## 5. LED báo gì

| LED | Nghĩa |
|---|---|
| tắt | máy xông đang tắt, mọi thứ bình thường |
| sáng liên tục | máy xông đang chạy |
| nháy nhanh (0,1s) | đang ở chế độ **học mã** |
| nháy chậm (0,5s) | **có lỗi**: mất số đo cảm biến, hoặc chưa học mã IR |

Bốn trạng thái, cố ý không thêm: nhiều hơn thì không ai phân biệt nổi.

---

## 6. Vì sao không nối cloud

Việc của bo này là một vòng kín hoàn toàn cục bộ — đo phòng, quyết định, bắn IR. Không
bước nào cần tới server. Nối MQTT vào thì phải cấp cho nó một hàng `devices`, một token,
một loại thiết bị mới ở backend, và đổi lại được đúng một thứ: **nó sẽ ngừng hoạt động
khi rớt mạng**. Đó là cái giá sai cho một thiết bị mà toàn bộ giá trị nằm ở chỗ nó tự chạy.

Muốn thêm sau này thì chỗ móc vào là `DiffuserControl::status()` — nó đã trả về đủ mọi
thứ cần báo cáo.

WiFi và Bluetooth bị **tắt hẳn** lúc khởi động (`WiFi.mode(WIFI_OFF)` + `btStop()`). Lý
do chính không phải tiết kiệm điện mà là **nhiệt**: DHT22 nằm cách con ESP32 vài
centimet, module tự sinh nhiệt thì đẩy nhiệt độ đo lên và **kéo độ ẩm tương đối xuống**.
Sai lệch đó lệch một chiều nên làm mượt bao nhiêu cũng không gỡ ra được, và hậu quả là
máy xông chạy nhiều hơn cần thiết mà không có triệu chứng nào.

---

## 7. Thư mục này tự chứa hoàn toàn — có chủ đích

Nó **không** `src_dir` và **không** `build_src_filter` sang `../esp32-s3-panel/` như
`esp32-qrbox` và `esp32-s3-gateway` vẫn làm. Hai env kia là **ba bo cùng đóng một vai**
(panel treo tường) nên bắt buộc phải là một bản mã. Bo này đóng vai khác hẳn — không nối
cloud, không thi hành lệnh của server, không màn.

**Cái giá, nói thẳng:** phần thu/phát IR ở đây là bản viết lại gọn của
[`../esp32-s3-panel/src/ir-io.cpp`](../esp32-s3-panel/src/ir-io.cpp). Hai bài học đắt
nhất của file đó đã được mang sang nguyên vẹn và ghi rõ nguồn trong
[`src/ir-remote.cpp`](src/ir-remote.cpp):

1. `enableIRIn()` gọi chồng nhau → mắt thu **chết câm** (ESP-IDF từ chối gắn ngắt lần
   hai, log vẫn thản nhiên nói đang chờ bấm remote)
2. Tắt mắt thu trong lúc bắn → khỏi tự thu lại chính khung của mình

**Sửa lỗi ở một bên thì ngó sang bên kia.**

Kho mã thì **không** viết lại — nó đơn giản hơn hẳn
[`ir-store.cpp`](../esp32-s3-panel/src/ir-store.cpp) của panel. Panel phải giữ 18 tổ hợp
khoá theo UUID 36 ký tự trong khi tên khoá NVS chỉ được 15, nên nó cần băm FNV-1a, khoá
đối chiếu chống đụng băm, bảng bí danh, id tạm và cơ chế dọn rác. Ở đây chỉ có **hai ô**
và tên nhét thẳng vào khoá được (`"rON"`), nên toàn bộ tầng đó là thừa.

---

## 8. Sự cố hay gặp

| Triệu chứng | Nguyên nhân thường gặp nhất |
|---|---|
| Nạp xong, serial im hoàn toàn | Ai đó thêm `-D ARDUINO_USB_MODE=1` vào `platformio.ini`. Bo này dùng chip cầu CP210x, **không** phải USB gốc — xem chú thích trong file đó |
| `KHONG CO SO DO` mãi | Thiếu trở kéo trên đường DHT22, hoặc cắm nhầm chân, hoặc nuôi 5V |
| Học mã hết giờ mãi | Mắt thu cắm ngược (3 chân, dễ nhầm), nuôi sai áp, hoặc hết pin remote |
| Học xong mà máy không nhúc nhích | **Kiểm giao thức trước** (§2): `UNKNOWN` thì gần như chắc là học nhầm nhiễu đèn — `wipe`, tắt đèn, học lại. Nếu đã ra `NEC` thì mới nghi tầm phát / chưa hàn LED |
| Không thu mà cũng không phát | **Đảo nhầm GPIO14 ↔ GPIO27.** Hai module nhìn na ná nhau, mà đảo thì hỏng đúng cả hai chiều cùng lúc. Gõ `diag` là biết ngay |
| Máy chạy ngược pha với mong đợi | Remote bập bênh + niềm tin lệch — xem §4, bấm nút BOOT một lần |
| Máy tự tắt sau 4 tiếng, không bật lại | Đúng thiết kế: giới hạn giờ chạy + khoá 30 phút. **Đi xem bình nước** |
| Log đỏ `nvs_open failed` | Phân vùng NVS hỏng — `esptool erase_flash` rồi nạp lại (mất hết mã đã học) |

---

## 9. Liên quan

- [`../esp32-s3-panel/src/ir-io.cpp`](../esp32-s3-panel/src/ir-io.cpp) — bản gốc của phần IR
- [`../esp32-room/`](../esp32-room/) — 4 node góc phòng, cũng dùng DHT22 (`DHTesp`, không phải bản Adafruit)
- [`../esp32-outdoor/`](../esp32-outdoor/) — node ngoài trời, **cùng loại bo** ESP32 DevKit V1
- `src/app/comfort/mode_decision.py` — deadband + dwell ở phía backend
