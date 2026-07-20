# Aircon — Firmware node (bản TEST)

Hai node cảm biến cho **1 nhà** (khách hàng *Khách hàng*):

| Thư mục | Chip | Loại node | Cổng (máy này) | Hiện trên app/web |
|---|---|---|---|---|
| `esp32-indoor/` | ESP32 DevKit V1 | **Trong nhà** (master + IR) — *bản đang dùng* | | "Trong nhà" |
| `esp32s3-indoor-master/` | ESP32‑S3 | Trong nhà (master, **không IR**) — *dự phòng* | COM6 (CH343) | "Trong nhà" |
| `esp8266-outdoor/` | ESP8266 | **Ngoài trời** | COM5 (CP210x) | "Ngoài trời" |

> **`esp32-indoor/` thay thế `esp32s3-indoor-master/`**, không chạy song song — xem [§6](#6-node-trong-nhà-esp32-devkit-v1--ir-bản-đầy-đủ). Bo S3 giữ lại làm dự phòng nếu cần quay về bản chỉ-đo-nhiệt-độ.

---

## 1. Cắm chân DHT11

DHT11 có 2 loại: **module 3 chân** (đã có trở kéo — nối thẳng) hoặc **cảm biến 4 chân rời** (mắc thêm điện trở 10kΩ giữa DATA và 3V3).

### ESP32‑S3 (node trong nhà)
```
   DHT11                ESP32-S3
  ┌───────┐
  │  +  / VCC │──────── 3V3
  │  S  / DATA│──────── GPIO4   (chân silk "4" / "G4")
  │  -  / GND │──────── GND
  └───────┘
```

### ESP8266 NodeMCU/Wemos (node ngoài trời)
```
   DHT11                NodeMCU (ESP8266)
  ┌───────┐
  │  +  / VCC │──────── 3V3
  │  S  / DATA│──────── D2   (= GPIO4)
  │  -  / GND │──────── GND (G)
  └───────┘
```

⚠️ Cấp nguồn DHT11 bằng **3V3**, không dùng 5V (chân DATA vào GPIO 3.3V).
Muốn đổi chân: sửa `DHT_PIN` trong `src/config.h`.

---

## 2. Điền cấu hình (`src/config.h` mỗi node)

Lấy giá trị từ **web admin → Khách hàng "Khách hàng" → mở từng node → mục "Nạp firmware"**:

| Điền vào config.h | Lấy ở panel | Ghi chú |
|---|---|---|
| `ORG_ID` | ô ORG_ID | **giống nhau** cả 2 node |
| `DEVICE_UUID` | ô DEVICE_UUID | **khác nhau** mỗi node |
| `MQTT_USERNAME` | = DEVICE_UUID | |
| `MQTT_PASSWORD` | ô MQTT_PASSWORD | **khác nhau** mỗi node |
| `MQTT_HOST` | ô MQTT_HOST | **giống nhau**, dạng `*.emqxsl.com` |
| `MQTT_PORT` | 8883 | cố định |

WiFi (`WIFI_SSID`/`WIFI_PASSWORD`): điền mạng tại nơi lắp node.

---

## 3. Build & nạp (PlatformIO)

```bash
# Node trong nhà (ESP32-S3) — cắm cổng COM6
cd esp32s3-indoor-master
pio run -t upload --upload-port COM6
pio device monitor -p COM6 -b 115200      # xem log

# Node ngoài trời (ESP8266) — cắm cổng COM5
cd esp8266-outdoor
pio run -t upload --upload-port COM5
pio device monitor -p COM5 -b 115200
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
| `rc=-2` lặp mãi | TLS/mạng | Kiểm tra MQTT_HOST đúng, WiFi có internet; ESP8266 thiếu RAM → thử `net.setBufferSizes(512,512)` |
| `DHT11 doc loi (NaN)` | Sai chân/nguồn DHT11 | Kiểm lại VCC 3V3 + DATA đúng chân + (loại 4 chân) trở kéo 10k |
| Serial không ra chữ (S3) | Cắm nhầm cổng USB‑native | Cắm cổng "UART", hoặc bật `-D ARDUINO_USB_CDC_ON_BOOT=1` trong `platformio.ini` |

---

## 6. Node trong nhà `esp32-indoor/` (ESP32 DevKit V1 + IR) — bản đầy đủ

Node này gộp **3 vai trò** vào một bo: đo DHT · điều khiển máy lạnh bằng hồng ngoại ·
làm master nhận ESP‑NOW từ node ngoài trời.

### 6.1 Vì sao gộp thay vì tách "indoor" và "indoor‑master"

Hai ràng buộc từ backend, không phải lựa chọn thẩm mỹ:

1. **Mỗi org chỉ được có ĐÚNG 1 node `node_type=indoor`.**
   `telemetry_service.get_device_by_org_and_node()` dùng `scalar_one_or_none()` → tạo hàng
   device "indoor" thứ hai sẽ làm worker ném `MultipleResultsFound` và **toàn bộ luồng điều
   khiển tự động đứng**.
2. **Lệnh IR quá to để đi qua ESP‑NOW.** `command_publisher.py` gửi kèm `ir_raw` — mảng vài
   trăm mốc thời gian µs, cỡ vài KB. ESP‑NOW giới hạn **250 byte/gói**, muốn trung chuyển
   qua master thì phải tự viết giao thức chia mảnh + ghép lại + báo thiếu mảnh. Nối MQTT
   thẳng thì broker đã lo sẵn.

→ Node này **mang chính `DEVICE_UUID` của node ESP32‑S3 cũ**. Không tạo device mới trên web.

### 6.2 Cắm dây

Module thu và phát là hai bo rời, mỗi bo 3 chân:

```
   DHT11/DHT22            ESP32 DevKit V1
   VCC ──────────────────── 3V3
   DATA ─────────────────── GPIO4
   GND ──────────────────── GND

   IR Receiver (mắt thu)   ESP32 DevKit V1
   VCC ──────────────────── 3V3      ← KHÔNG dùng 5V (chân OUT sẽ đẩy 5V vào GPIO 3.3V)
   OUT ──────────────────── GPIO27
   GND ──────────────────── GND

   IR Transmitter (LED phát)
   VCC ──────────────────── 3V3
   DAT ──────────────────── GPIO26
   GND ──────────────────── GND
```

Chọn GPIO26/27 vì chúng **không phải chân strapping** và không bị ràng buộc lúc boot (khác
GPIO0/2/5/12/15). Đổi chân thì tránh GPIO6‑11 (nối flash) và GPIO34‑39 (chỉ vào, không phát
được). Sửa `IR_TX_PIN`/`IR_RX_PIN` trong `src/config.h`.

**Đặt bo ở đâu:** LED phát trên module này được kéo thẳng từ chân dữ liệu, không có
transistor khuếch đại → **tầm với chỉ ~2‑5 m và cần nhìn thẳng vào mắt nhận của dàn lạnh**.
Muốn xa hơn phải tự thêm transistor + LED công suất. Mắt thu để hướng ra phía người dùng
đứng bấm remote.

### 6.3 Nạp

```bash
cd esp32-indoor
cp src/config.h.example src/config.h    # rồi điền như §2
pio run -t upload --upload-port COMx
pio device monitor -p COMx -b 115200
```

Log chạy đúng:
```
WiFi -> "TEN_WIFI" .... OK  IP=192.168.x.x
MQTT ... connected
ESP-NOW san sang · MAC master = XX:XX:... · kenh 6
IR: phat GPIO26 · thu GPIO27
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

> `espnow-relay.*` và `slave-watch.*` là **bản sao** từ `esp32s3-indoor-master/src/` — cố ý
> giữ hai project độc lập build được, đổi lại phải sửa cả hai nơi nếu logic ESP‑NOW thay đổi.

---

## Câu hỏi mở
- **Auth EMQX cho device mới:** backend lưu `mqtt_token` per‑device; cần xác nhận cơ chế nạp cặp (username=DEVICE_UUID, password=token) vào EMQX built‑in DB đã tự chạy khi tạo device trên web chưa. Nếu chưa, `rc=5` sẽ xuất hiện và phải seed thủ công.
