# BreezeLink — Firmware node

Sáu thiết bị cho **1 nhà** (khách hàng *Khách hàng*):

| Thư mục | Bo | Loại node | Hiện trên app/web |
|---|---|---|---|
| `esp32-s3-panel/` | **Bo 2.8" ESP32‑S3** (ILI9341V + FT6336G) | **Gateway / panel treo tường CHÍNH THỨC** — thu ESP‑NOW, IR, màn cảm ứng, UART tới UNO Q, **không cảm biến**. Giữ **mã nguồn** của cả ba env | "Gateway trong nhà" |
| `esp32-qrbox/` | QR Box Advance Touch Screen (ESP32‑WROOM‑32E‑N8 + màn 2.8") | Bo cũ (hỏng mạch nạp) — chỉ còn cấu hình build | — |
| `esp32-room/` | 4× **ESP32‑C3‑DevKitM‑1** + DHT22 | **Cảm biến phòng** — slave ESP‑NOW | "Cảm biến trong phòng" |
| `esp32-outdoor/` | ESP32 DevKit V1 | **Ngoài trời** — slave ESP‑NOW | "Nút ngoài trời" |
| `esp32-s3-gateway/` | ESP32‑S3‑DevKitC‑1 | **Bo gỡ lỗi** — cùng mã, không màn, in mọi gói vào/ra ra serial | — |
| (không ở đây) | Arduino UNO Q | Edge AI — nối gateway qua **UART**, xem [`../edge-ai/`](../edge-ai/) | — |

> **PANEL CHÍNH THỨC LÀ `esp32-s3-panel/`.** Bo QR Box Advance hỏng mạch nạp — esptool
> dò được chip nhưng mọi lần ghi khối dữ liệu đều đứt giữa chừng, cả hai chiều, không
> phụ thuộc baud.
>
> **Mã nguồn panel nằm ở `esp32-s3-panel/src/` — đúng MỘT bản.** Hai thư mục
> `esp32-qrbox/` và `esp32-s3-gateway/` không có mã riêng, chúng trỏ ngược về đó
> (`src_dir` / `build_src_filter`), chỉ khác cờ build. Chép mã ra là tạo ra hai panel
> lệch nhau ngay lần sửa đầu tiên. Chỗ nào khác nhau giữa các bo thì để ở
> `esp32-s3-panel/src/board-pins.h`, chọn bằng `-D BOARD_*`.
>
> Sơ đồ chân và trình tự bring-up bo mới: [`esp32-s3-panel/README.md`](esp32-s3-panel/README.md).

> **GATEWAY KHÔNG CÒN ĐO NHIỆT ĐỘ.** Cả DHT22 lẫn SHT3x đã bỏ khỏi firmware của nó:
> một cảm biến treo tường chỉ đo được *cái tường đó*, còn bốn góc phòng chênh nhau
> 3–4 °C là chuyện thường. Số "trong nhà" nay là **trung vị** các góc còn tươi
> (`esp32-s3-panel/src/room-registry.h`).

> **CẢ HAI NODE ĐỀU LÀ ESP32.** Trước đây mỗi node một dòng chip (indoor ESP32‑S3,
> outdoor ESP8266) nên hai bên dùng hai bộ API ESP‑NOW khác hẳn nhau — sửa giao
> thức là phải sửa hai lần theo hai kiểu, đúng loại việc dễ quên một nửa. Nay
> chung một API (`esp_now.h` của IDF), chung toolchain, chung cách nạp.
>
> Hai thư mục cũ `esp32s3-indoor-master/` (bản thử nghiệm không IR) và
> `esp8266-outdoor/` **đã bỏ** — xem lịch sử git nếu cần tra lại.

---

## 1. Cắm cảm biến nhiệt/ẩm

**Hai node dùng hai loại cảm biến khác nhau** — không phải tuỳ hứng mà do bo:

### Node ngoài trời (ESP32 DevKit V1) — DHT22

DHT có 2 loại: **module 3 chân** (đã có trở kéo — nối thẳng) hoặc **cảm biến 4 chân rời** (mắc thêm điện trở 10kΩ giữa DATA và 3V3).

```
   DHT22/DHT11          ESP32 DevKit V1
  ┌───────────┐
  │  +  / VCC │──────── 3V3
  │  S  / DATA│──────── GPIO4   (chân silk "D4" / "G4")
  │  -  / GND │──────── GND
  └───────────┘
```

⚠️ Cấp nguồn DHT bằng **3V3**, không dùng 5V (chân DATA vào GPIO 3.3V).
Muốn đổi chân: sửa `DHT_PIN` trong `src/config.h`. Nhớ đặt `DHT_TYPE` đúng loại —
khai sai vẫn đọc được (checksum vẫn pass) nhưng ra số vô lý (~1.8°C / ~23%).

### Node góc phòng (ESP32‑C3‑DevKitM‑1) — DHT22 trên GPIO4

Bốn bo giống hệt nhau, chỉ khác `DEVICE_UUID`. DATA → GPIO4, **trở kéo 4.7k lên 3.3V**,
nuôi bằng **3.3V không phải 5V**. Chân phải tránh trên C3 và lý do:
[`esp32-room/README.md`](esp32-room/README.md).

### Gateway (QR Box Advance) — **KHÔNG cắm cảm biến nào**

Bo này từng có tuỳ chọn SHT3x trên I²C 0x44; đã bỏ hẳn khỏi firmware. Cắm vào cũng
không có tác dụng — không còn mã nào đọc nó, và có đọc thì cũng chỉ thêm một nguồn
số thứ hai khiến màn nhảy qua lại giữa nhiệt độ của cái tường và nhiệt độ căn phòng.

---

## 2. Điền cấu hình (`src/config.h` mỗi node)

Lấy giá trị từ **web admin → Khách hàng "Khách hàng" → mở từng node → mục "Nạp firmware"**:

| Điền vào config.h | Lấy ở panel | Ghi chú |
|---|---|---|
| `ORG_ID` | ô ORG_ID | **giống nhau** cả 2 node |
| `DEVICE_UUID` | ô DEVICE_UUID | **khác nhau** mỗi node |
| `MQTT_USERNAME` | = DEVICE_UUID | |
| `MQTT_PASSWORD` | ô MQTT_PASSWORD | **khác nhau** mỗi node |
| `MQTT_HOST` | ô MQTT_HOST | **giống nhau** — xem cảnh báo dưới |
| `MQTT_PORT` | 1883 | cố định (plaintext, KHÔNG TLS) |

> ⚠️ **Panel web ghi `MQTT_HOST = "emqx"`** — đó là tên service nội bộ Docker, ESP
> không phân giải được. Phải điền **IP/domain công khai** của server.

WiFi (`WIFI_SSID`/`WIFI_PASSWORD`): điền mạng tại nơi lắp node.

Riêng node ngoài trời chạy env `esp32-espnow` thì **không đăng nhập WiFi** — nó chỉ
*quét* để biết router đang phát ở kênh nào rồi bám theo (ESP‑NOW bắt buộc hai bên
cùng kênh). Với env đó chỉ `WIFI_SSID` có tác dụng, và cả khối MQTT bị bỏ qua.

---

## 3. Build & nạp (PlatformIO)

```bash
# Panel treo tường CHÍNH THỨC (bo 2.8" ESP32-S3) — cắm thẳng cổng USB-C
# ĐỌC esp32-s3-panel/README.md §4 TRƯỚC KHI HÀN: 3 thứ phải đo trên bo thật.
cd esp32-s3-panel
cp src/config.h.example src/config.h      # điền như §2 — DÙNG CHUNG cho mọi bo panel
pio run -e esp32s3-panel -t upload --upload-port COMx
pio device monitor -p COMx -b 115200      # xem log

# Bo QR Box cũ — USB-TTL cắm vào cổng P3. Không có src/ riêng, dùng chung
# esp32-s3-panel/src/ ở trên, nên KHÔNG phải điền config.h lần nữa.
cd esp32-qrbox
pio run -e qrbox-touch -t upload --upload-port COMx

# 4 node góc phòng — nạp LẦN LƯỢT, đổi DEVICE_UUID giữa mỗi lần
cd esp32-room
cp src/config.h.example src/config.h      # WIFI_SSID (chỉ dò kênh) + DEVICE_UUID
pio run -e esp32c3-room -t upload --upload-port COMz

# Node ngoài trời (ESP32 DevKit V1)
cd esp32-outdoor
cp src/config.h.example src/config.h
pio run -e esp32-espnow -t upload --upload-port COMy   # MẶC ĐỊNH: slave ESP-NOW
pio device monitor -p COMy -b 115200
```

> ⚠️ **Bo QR Box Advance phải có nguồn riêng 9–24 VDC ở P2/P4.** Cổng P3 chỉ có
> TX/RX/GND cho debug — cấp mỗi USB‑TTL thì màn + ESP32 không đủ dòng, bo sụt áp
> và **reset lặp liên tục** (log ra `rst:0x3 (SW_RESET)` mỗi vài chục ms, không
> bao giờ in nổi một dòng của firmware).

Node ngoài trời có **env dự phòng** tự nối WiFi + MQTT thẳng, dùng khi ESP‑NOW
trục trặc — nó không phụ thuộc node trong nhà nên tách lỗi rất nhanh:

```bash
pio run -e esp32-wifi -t upload --upload-port COMy
```

Log chạy đúng sẽ thấy:
```
WiFi -> "TEN_WIFI" .... OK  IP=192.168.x.x
MQTT ... connected
[telemetry] t=30.0°C h=60% -> da gui
```

---

## 4. Kiểm tra đồng bộ

- **Web:** node hiện **"Trực tuyến"**; mục *Nhiệt độ/Độ ẩm mới nhất* + biểu đồ cập nhật.
- **App:** thẻ thiết bị hiện *"Nhiệt độ trong nhà"* (ESP32) và *"Nhiệt độ ngoài trời"* (ESP8266).

---

## 5. Xử lý lỗi thường gặp

| Log | Nguyên nhân | Cách xử lý |
|---|---|---|
| `MQTT ... that bai rc=4` | Sai username/password | Copy lại DEVICE_UUID/MQTT_PASSWORD từ panel |
| `MQTT ... that bai rc=5` | Broker chưa cấp quyền cho device | Cần nạp cặp user/token của device vào EMQX (seed/sync auth) — xem *Câu hỏi mở* |
| `rc=-2` lặp mãi | Mạng/host | Kiểm tra MQTT_HOST là IP công khai (không phải `emqx`), WiFi có internet |
| `Doc cam bien loi (NaN)` *(node ngoài trời)* | Sai chân/nguồn/loại DHT | Kiểm lại VCC 3V3 + DATA đúng chân + (loại 4 chân) trở kéo 10k + `DHT_TYPE` đúng |
| `Chua co so do SHT3x` *(node trong nhà)* | Chưa câu dây SHT3x vào bus I²C, hoặc sai địa chỉ | Câu vào J1 (§1); SHT3x phải ở 0x44 |
| `rst:0x3 (SW_RESET)` lặp mỗi vài chục ms | Bo QR Box thiếu nguồn chính | Cấp 9–24 VDC vào P2/P4 — USB‑TTL ở P3 không nuôi nổi bo (§3) |

---

## 6. Gateway — panel treo tường (IR + màn cảm ứng)

Mã ở `esp32-s3-panel/src/`, nạp lên bo `esp32-s3-panel/` (chính thức) hoặc bo QR Box cũ.

Bo này gộp **5 vai trò** — nhưng **đo nhiệt độ không còn là một trong số đó**: thu ESP‑NOW
từ 4 node góc phòng và node ngoài trời · trung chuyển lên MQTT hộ từng node · điều khiển
máy lạnh bằng hồng ngoại · hiển thị + điều khiển tại chỗ trên màn 2.8" · nói chuyện với
Arduino UNO Q qua UART. Cả năm đều là *chuyển tiếp* và *thi hành*; không vai trò nào
tạo ra một phép đo.

Thiết kế giao diện, cách đọc ngược sơ đồ chân từ schematic, và lý do phải tách hai lõi:
[`Interface/README.md`](Interface/README.md).

### 6.1 Vì sao gateway nối MQTT thẳng thay vì làm một slave nữa

1. **Đích của lệnh là một lookup, không phải suy luận.** `get_gateway_device()` chọn
   `role=master` trước (và **từ chối** node `room` — chúng không có phiên MQTT nào), rồi mới
   rơi về `node_type=indoor` cũ nhất. Nên **phải đặt vai trò master cho bo này trên web**;
   không đặt thì hộ nào có hai hàng indoor sẽ bị lái nhầm node, và triệu chứng là lệnh
   publish thành công mà máy lạnh không nhúc nhích.
2. **Lệnh IR quá to để đi qua ESP‑NOW.** `command_publisher.py` gửi kèm `ir_raw` — mảng vài
   trăm mốc thời gian µs, cỡ vài KB. ESP‑NOW giới hạn **250 byte/gói**, muốn trung chuyển
   qua master thì phải tự viết giao thức chia mảnh + ghép lại + báo thiếu mảnh. Nối MQTT
   thẳng thì broker đã lo sẵn.

→ Node này **mang chính `DEVICE_UUID` của node ESP32‑S3 cũ**. Không tạo device mới trên web.

### 6.2 Cắm dây

Module thu và phát là hai bo rời, mỗi bo 3 chân. Trên bo QR Box Advance **không còn
GPIO trống nào**, nên hai chân IR phải lấy từ hai chỗ khác nhau — đây là ràng buộc
phần cứng, không phải lựa chọn:

```
   IR Transmitter (LED phát)   QR Box Advance
   VCC ──────────────────────── 3V3
   DAT ──────────────────────── GPIO17   ← chân TX của module 4G A7680C
   GND ──────────────────────── GND

   IR Receiver (mắt thu)
   VCC ──────────────────────── 5V       ← có sẵn trên header P3
   OUT ──────────────────────── GPIO15   ← P3, đi qua bộ dịch mức TXS0104
   GND ──────────────────────── GND
```

- **IR phát → GPIO17** chỉ dùng được khi **KHÔNG hàn module 4G A7680C** lên bo (dự án
  này không dùng 4G). Chân này **không ra header** — phải hàn dây thẳng vào pad chân
  module. Chạy 3.3V trực tiếp: đường UART_2 **không** đi qua TXS0104.
- **IR thu → GPIO15** đi qua TXS0104 nên mắt thu chạy được ở 5V. Bộ dịch mức này chỉ
  thông khi `EN_LEVEL_SHIFT` (GPIO12) được kéo HIGH — `Ui::begin()` làm việc đó *trong
  `setup()`*, và **tuyệt đối không được kéo lên bằng trở ngoài**: GPIO12 là MTDI, HIGH
  lúc reset thì ROM chọn mức flash 1.8V và **bo không boot**.

Hai chân này khai trong `build_flags` của `platformio.ini` (cùng chỗ với các cờ `TFT_*`),
**không phải** trong `src/config.h`.

**Đặt bo ở đâu:** LED phát trên module này được kéo thẳng từ chân dữ liệu, không có
transistor khuếch đại → **tầm với chỉ ~2‑5 m và cần nhìn thẳng vào mắt nhận của dàn lạnh**.
Muốn xa hơn phải tự thêm transistor + LED công suất. Mắt thu để hướng ra phía người dùng
đứng bấm remote.

### 6.3 Nạp

```bash
# config.h nằm ở esp32-s3-panel/src/ — dùng chung cho mọi bo panel (§3)
cd esp32-qrbox
pio run -e qrbox-touch -t upload --upload-port COMx
pio device monitor -p COMx -b 115200
```

Log chạy đúng:
```
== BreezeLink · QR Box Advance Touch · TRONG NHA (indoor + master + IR) ==
LCD: ST7789 320x240 (rotation 1) · cam ung: GT911
WiFi -> "TEN_WIFI" .... OK  IP=192.168.x.x
MQTT ... connected
ESP-NOW san sang · MAC master = XX:XX:... · kenh 6
IR: phat GPIO17 · thu GPIO15
[telemetry] t=30.0°C h=60% -> da gui · espnow nhan=0 bo=0 · kenh=6
```

### 6.4 Học remote (bắt buộc làm trước khi auto‑control chạy được)

Trên web/app bấm **"Học nút này"** cho từng mục. Backend gửi `{"learn":"COOL 25"}` xuống
topic `cmd`, node bật mắt thu 30s, bạn bấm nút tương ứng trên remote thật, node gửi dạng
sóng lên topic `learn` và backend lưu vào `ir_codes`.

Cần học đủ (theo `ir_service._REQUIRED_*`): **COOL 24, 25, 26, 27, 28** + **DRY**, **FAN**,
**OFF**. Thiếu bất kỳ mục nào thì `comfort_engine` ném `NoIrCodesError` và **auto‑control bị
khoá hoàn toàn**. Các nút rời (`FAN_SPEED`, `SLEEP`, `SWING_V`…) là tuỳ chọn.

Log khi học:
```
[learn] "COOL 25" — huong remote vao mat thu roi bam nut (toi da 30s)
[learn] "COOL 25" 227 moc (1348 byte) -> da gui len cloud
```

### 6.5 Nhận lệnh

```
[cmd] c-1a2b3c4d -> COOL 26 (auto:COOL@26) · 227 moc, cho phat
[cmd] da luu ma 7f3e…-… vao NVS (227 moc)
[ir] da phat 227 moc ra may lanh
[state] ack=c-1a2b3c4d mode=COOL setpoint=26 -> da gui
```

Backend chỉ gửi kèm `ir_raw` **lần đầu** của mỗi `ir_code_id`; những lần sau nó tin node đã
giữ mã trong NVS và chỉ gửi id. Mã trong NVS **sống qua mất điện và qua cả `pio run -t
upload`** (NVS nằm ở phân vùng riêng).

### 6.6 Xử lý lỗi riêng của node này

| Log | Nguyên nhân | Cách xử lý |
|---|---|---|
| `ir_code_id=… khong co trong NVS ma server khong gui kem ir_raw` | **Lệch cache**: node bị `erase_flash`/đổi bo nên mất NVS, nhưng Redis phía server vẫn nhớ "node đã có mã này" | Xoá cache IR của org trong Redis (key của `redis_ir_cache`) để server gửi lại `ir_raw`. Node **cố ý không ack** trong trường hợp này để `commands.acked_at` trên web không báo sai là đã thi hành |
| `[learn] het gio cho "…"` | Remote hết pin, không hướng đúng mắt thu, hoặc quá xa | Bấm cách mắt thu < 1 m, chĩa thẳng vào |
| `[ir] bo qua nhieu (3 moc)` | Đèn huỳnh quang/remote khác lọt vào | Bình thường — node vẫn đang chờ, cứ bấm remote |
| `[cmd] … chua hoc ma nay` | Chưa học mã cho (mode, nhiệt độ) đó | Xem §6.4, học đủ bộ |
| Máy lạnh không phản ứng dù log báo `da phat` | Ngoài tầm/lệch hướng LED phát | Xem ghi chú tầm với ở §6.2 |
| `Khong cap phat duoc bo dem MQTT` | Hết heap lúc khởi động | Hiếm; nếu gặp thì giảm `MQTT_BUFFER_BYTES` và `IrIo::RAW_MAX` |
| Học mã IR luôn hết giờ, dù remote tốt | `EN_LEVEL_SHIFT` chưa lên HIGH → TXS0104 chặn mắt thu | Xem §6.2; kiểm tra `Ui::begin()` có chạy không (màn có sáng không) |

> `espnow-relay.*` và `slave-watch.*` **từng** là bản sao chung với
> `esp32s3-indoor-master/`. Thư mục đó đã bỏ, nên nay `esp32-s3-panel/src/` là **nơi
> duy nhất** giữ chúng — sửa một chỗ là xong, không còn phải nhớ đồng bộ hai nơi.
> Khuôn gói tin thì vẫn dùng chung với node ngoài trời qua `shared/espnow-message.h`.

---

## Câu hỏi mở
- **Auth EMQX cho device mới:** backend lưu `mqtt_token` per‑device; cần xác nhận cơ chế nạp cặp (username=DEVICE_UUID, password=token) vào EMQX built‑in DB đã tự chạy khi tạo device trên web chưa. Nếu chưa, `rc=5` sẽ xuất hiện và phải seed thủ công.
