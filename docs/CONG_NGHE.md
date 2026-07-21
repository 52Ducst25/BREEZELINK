# Công nghệ dự án & định hướng bổ sung

Tài liệu này tổng hợp công nghệ đang dùng trong dự án Aircon (Frontend, Backend, Hardware,
Firmware, Hạ tầng), **so sánh với các công nghệ tương tự** ở mỗi lớp, và giải thích lý do
chọn công nghệ hiện tại thay vì phương án kia — trong khi vẫn giữ nguyên nguyên tắc topology
phần cứng:

> 1 node **indoor** điều khiển 1 máy lạnh · 1 node **outdoor** đo nhiệt độ/độ ẩm ngoài trời
> · máy lạnh được điều khiển qua **IR transmitter/receiver**.

> **Cập nhật (đã triển khai):** vai trò *indoor-master* (trung chuyển ESP-NOW) trước đây tách
> thành node riêng, nay **gộp vào chính node indoor** — xem §6 "Đề xuất bổ sung" mục 1-2 và
> `FirmWare/README.md` §6.1. Lý do gộp: lệnh IR mang `ir_raw` cỡ **vài KB**, vượt xa giới hạn
> **250 byte/gói** của ESP-NOW nên không thể trung chuyển qua master. Topology thực tế còn
> **2 node/hộ**.
>
> **Đính chính 21/07/2026:** trước đây phần này còn nêu ràng buộc thứ hai — *"backend chỉ chấp
> nhận đúng 1 node `node_type=indoor` mỗi org vì `get_device_by_org_and_node` dùng
> `scalar_one_or_none`"*. **Ràng buộc đó đã được gỡ.** Hàm này nay **suy biến an toàn**: gặp
> nhiều node indoor thì chọn node cũ nhất + ghi log cảnh báo, không còn ném
> `MultipleResultsFound` làm sập toàn bộ luồng điều khiển. Tạo nhiều node indoor giờ **không
> làm hỏng gì**, nhưng **chỉ node cũ nhất được điều khiển** cho tới khi làm xong Phase 2
> (quyết định comfort riêng cho từng phòng). Lý do (b) — giới hạn 250 byte của ESP-NOW — vẫn
> nguyên giá trị và tự nó đã đủ để giữ quyết định gộp.

Phần lý do lựa chọn là phân tích kỹ thuật dựa trên ràng buộc thực tế của dự án (quy mô
multi-tenant nhỏ, 1 VPS dùng chung với 2 dự án khác, đội phát triển nhỏ, thiết bị nhúng giá
rẻ, thị trường Việt Nam) — không phải trích dẫn tài liệu quyết định chính thức nào, vì repo
chưa có ADR (Architecture Decision Record) riêng.

---

## Mục lục

- [1. Backend](#1-backend)
- [2. Frontend — App Flutter](#2-frontend--app-flutter)
- [3. Hardware](#3-hardware)
- [4. Firmware](#4-firmware)
- [5. Hạ tầng / Triển khai](#5-hạ-tầng--triển-khai)
- [6. Nghiên cứu (`Research/`) & đề xuất bổ sung](#6-nghiên-cứu-research--đề-xuất-bổ-sung)

---

## 1. Backend

| Lớp | Đã chọn | Phương án tương tự | Vì sao chọn cái này thay vì cái kia |
|---|---|---|---|
| Framework web | **FastAPI** | Django, Flask, NestJS/Express (Node.js), Spring Boot | Cần **async native** để 1 tiến trình vừa phục vụ HTTP vừa chạy worker MQTT + WebSocket realtime không block nhau. Pydantic tích hợp sẵn giảm boilerplate validate, tự sinh Swagger/OpenAPI giúp phát triển song song với app Flutter. So với Django: không cần admin-panel/ORM sync mặc định của Django cho một service quy mô vừa — Django sẽ nặng hơn mức cần thiết. So với Flask: Flask không có validate/async built-in, phải tự ghép nhiều thư viện. So với Node/NestJS: hệ sinh thái Python có sẵn thư viện xử lý số (thuật toán comfort, EMA...) và MQTT async (`aiomqtt`) thuận tiện hơn. |
| ORM + migration | **SQLAlchemy 2 (async) + Alembic** | Django ORM, Tortoise ORM, Prisma (Python, còn early), raw SQL/asyncpg | SQLAlchemy 2 là ORM async trưởng thành nhất trong hệ Python, khớp tự nhiên với FastAPI async. Alembic là công cụ migration chuẩn đi kèm, cần thiết vì schema multi-tenant phải thay đổi an toàn qua từng phiên bản có thể rollback. Tortoise ORM nhẹ hơn nhưng tooling migration kém ổn định hơn Alembic; Prisma Python chưa đủ chín để phụ thuộc vào production. |
| Cơ sở dữ liệu | **PostgreSQL 15** | MySQL/MariaDB, MongoDB, SQLite | Cần quan hệ chặt (multi-tenant org-scoping, khoá ngoại) **và** row-level locking mạnh (`SELECT ... FOR UPDATE` để chống tranh chấp mã kích hoạt dùng 1 lần) — Postgres làm tốt cả hai, cộng thêm JSONB cho cấu hình thuật toán linh hoạt theo từng khách mà vẫn giữ tính toàn vẹn quan hệ. MongoDB (NoSQL) khó đảm bảo transaction chặt cho race-condition mã kích hoạt và không cần thiết vì dữ liệu có cấu trúc rõ ràng (device, telemetry, org, user). MySQL khả thi nhưng thiếu JSONB + advisory lock mạnh như Postgres. |
| Cache / Pub-Sub | **Redis 7** | Memcached, Kafka, in-process pub/sub | Cần vừa cache state realtime của node, vừa pub/sub đẩy cập nhật tới WebSocket — Redis làm được cả hai trong 1 service, đơn giản hoá hạ tầng. Memcached không có pub/sub. Kafka quá nặng (cần Zookeeper/cluster) cho quy mô 1 VPS nhỏ. |
| MQTT client (backend) | **aiomqtt** | paho-mqtt (sync callback), gmqtt | Worker cần chạy async cùng event loop với DB/Redis mà không block. `paho-mqtt` gốc là sync/callback-based, phải chạy trong thread riêng → phức tạp hoá code trong stack async. `aiomqtt` (wrapper asyncio quanh paho) khớp tự nhiên hơn. |
| Xác thực | **JWT (HS256)** | Session/cookie thuần, OAuth2 bên thứ ba (Google/Facebook login) | App Flutter là client tách biệt với web, cần cơ chế **stateless** để tự lưu token cục bộ (`flutter_secure_storage`) và gọi REST/WebSocket không phụ thuộc cookie — JWT phù hợp multi-client (web + mobile) hơn session-cookie chỉ tiện cho SSR thuần. OAuth2 bên thứ ba không cần thiết vì danh tính khách hàng được xác định qua **mã kích hoạt do bên bán cấp**, không qua mạng xã hội. |
| Giao diện quản trị | **SSR Jinja2** (cùng codebase API) | SPA riêng (React/Vue) gọi cùng API | Trang quản trị chỉ dùng nội bộ (nhà bán/nhân viên), không cần UX phức tạp của SPA. SSR tái sử dụng trực tiếp tầng `services/` dùng chung với API, không cần thêm build pipeline JS, không cần xử lý CORS phức tạp. Một SPA riêng sẽ nhân đôi công sức (2 codebase, 2 pipeline deploy) cho lợi ích không tương xứng ở quy mô admin nội bộ này. |

---

## 2. Frontend — App Flutter

| Lớp | Đã chọn | Phương án tương tự | Vì sao chọn cái này thay vì cái kia |
|---|---|---|---|
| Nền tảng app | **Flutter** | React Native, native Android (Kotlin) + iOS (Swift) riêng, PWA | Cần 1 codebase chạy cả Android/iOS mà đội nhỏ vẫn duy trì được. Flutter compile AOT ra native code, mượt hơn cho UI custom (temp dial, biểu đồ realtime) so với React Native (qua JS bridge). Viết native 2 nền tảng riêng tốn gấp đôi công sức cho một team nhỏ. PWA bị giới hạn quyền truy cập native (secure storage bền vững, cài đặt APK OTA ngoài store) — không đáp ứng được luồng **tự cập nhật OTA** mà dự án cần (app không phân phối qua CH Play/App Store). |
| State management | **provider** | riverpod, bloc, getx | Ứng dụng hiện tại có state không quá phức tạp (dashboard, control, device list) — `provider` đủ dùng, được Google duy trì chính thức lâu năm, learning curve thấp hơn Bloc (nhiều boilerplate) hoặc Riverpod (khái niệm mới hơn, đường cong học tập dốc hơn cho lợi ích chưa cần ở quy mô app này). |
| Networking | **dio** | package `http` thuần | `dio` hỗ trợ interceptor (tự gắn JWT vào header, xử lý refresh token tập trung), timeout, cancel token — `http` thuần thiếu các tính năng này, phải tự viết thêm nhiều lớp bọc mà `dio` đã có sẵn. |
| Lưu credential | **flutter_secure_storage** | `shared_preferences` (lưu thô) | Credential đăng nhập ("ghi nhớ đăng nhập") cần mã hoá bằng Keystore/Keychain của hệ điều hành — `shared_preferences` lưu plaintext trong file, không phù hợp cho dữ liệu nhạy cảm. Dữ liệu không nhạy cảm (cấu hình UI...) vẫn dùng `shared_preferences` như bình thường. |
| Bản đồ | **flutter_map + OpenStreetMap** | `google_maps_flutter` | Không cần API key/billing (Google Maps yêu cầu bật billing dù có hạn mức miễn phí, phức tạp hoá vận hành cho một tính năng phụ là chọn vị trí nhà), giảm chi phí và rủi ro bị khoá key. Đủ dùng cho nhu cầu đơn giản là ghim toạ độ nhà khách hàng. |
| Kênh realtime | **WebSocket** | Long-polling, Server-Sent Events (SSE) | Cần cập nhật hai chiều độ trễ thấp (số đo trực tiếp + xác nhận lệnh điều khiển) — WebSocket phù hợp hơn SSE (chỉ một chiều server→client) và hiệu quả hơn long-polling (tránh mở lại kết nối liên tục, tốn tài nguyên server khi nhiều thiết bị/khách hàng cùng theo dõi). |

---

## 3. Hardware

| Vị trí | Đã chọn | Phương án tương tự | Vì sao chọn cái này thay vì cái kia |
|---|---|---|---|
| MCU node indoor (kiêm master + IR) | **ESP32 DevKit V1** (ESP32-WROOM-32) | ESP32-S3, ESP8266, Raspberry Pi Pico W, STM32 + module WiFi rời | Node này cùng lúc: relay ESP-NOW, IR blaster + IR learner, và giữ kết nối MQTT với bộ đệm 12KB (đủ chứa `ir_raw` vài KB) — cần nhiều RAM/flash hơn hẳn ESP8266. **Đã chuyển từ ESP32-S3 sang ESP32 classic**: firmware thực tế chỉ dùng 14.6% RAM / 65.4% flash, nên dư địa của S3 không được dùng tới, trong khi DevKit V1 rẻ hơn, sẵn hàng hơn ở VN và không có bẫy "2 cổng USB" (cổng USB-native cần cờ `ARDUINO_USB_CDC_ON_BOOT`) khiến người lắp mất thời gian. Bo S3 giữ lại làm dự phòng. STM32 cần thêm module WiFi rời (vd. ESP-01) → phức tạp hoá BOM và firmware một cách không cần thiết khi ESP32 đã tích hợp sẵn WiFi + BLE. |
| MCU node outdoor | **ESP8266** | ESP32-C3, module cảm biến Zigbee rời | Node outdoor chỉ đọc DHT và gửi dữ liệu định kỳ — tác vụ nhẹ, không cần BLE hay nhiều GPIO. ESP8266 rẻ hơn đáng kể so với dòng ESP32, tiết kiệm chi phí BOM khi tính năng cần thiết đã đủ. Zigbee cần thêm 1 coordinator riêng biệt — tăng độ phức tạp hạ tầng không cần thiết cho quy mô 1 outdoor/nhà. |
| Cảm biến nhiệt độ/độ ẩm | **DHT11 / DHT22** | SHT31/SHT35 (I2C), BME280/BME680, AHT20 | DHT rẻ, dễ mua tại Việt Nam, đủ độ chính xác (±0.5 °C với DHT22) cho việc tính running-mean chứ không cần độ chính xác phòng lab. SHT3x/BME280 chính xác hơn và giao tiếp I2C ổn định hơn 1-wire của DHT, nhưng đắt hơn 2-4 lần — là hướng nâng cấp hợp lý về sau nếu thuật toán comfort cần độ chính xác cao hơn (xem mục 6). |
| Giao tiếp giữa các node | **ESP-NOW** | Zigbee, LoRa, BLE mesh, WiFi mesh (ESP-MESH) | Không cần router/coordinator trung gian, độ trễ thấp, không tốn thêm phần cứng (built-in trên chip Espressif đang dùng), đủ tầm phủ trong phạm vi 1 căn nhà. Zigbee/LoRa cần thêm module/coordinator riêng — over-engineering cho topology cố định 1-hop (outdoor → master). BLE mesh phức tạp hoá provisioning. WiFi mesh (ESP-MESH) nặng hơn, cần định tuyến động không cần thiết khi topology đã cố định. |
| Điều khiển máy lạnh | **IR transmitter/receiver** | Relay đấu trực tiếp vào block máy nén, module RF 433 MHz, tích hợp qua API/app riêng của hãng máy | Đa số máy lạnh dân dụng tại Việt Nam vẫn dùng remote hồng ngoại chuẩn — IR là cách "universal", không can thiệp phần cứng máy lạnh (không mất bảo hành, không cần biết mạch điện riêng từng hãng), tương thích ngược với máy cũ không có smart module. Đấu relay trực tiếp vào block nén rủi ro cao (mất bảo hành, nguy cơ cháy nổ). RF 433 MHz không phải chuẩn giao tiếp mà máy lạnh sử dụng nên không khả thi. Tích hợp qua API hãng chỉ khả dụng với số ít model đời mới có sẵn WiFi — không đủ phổ quát cho một sản phẩm bán ra thị trường rộng. |

---

## 4. Firmware

| Lớp | Đã chọn | Phương án tương tự | Vì sao chọn cái này thay vì cái kia |
|---|---|---|---|
| Build system | **PlatformIO** | Arduino IDE, ESP-IDF thuần | PlatformIO quản lý dependency (`lib_deps`) và nhiều build environment trong cùng 1 project (`esp8266-espnow` vs `esp8266-wifi`) tốt hơn Arduino IDE (không quản lý version thư viện tốt, khó dùng cho CI). Vẫn giữ được framework Arduino (dễ viết, cộng đồng lớn, có sẵn thư viện IR/DHT) thay vì chuyển hẳn sang ESP-IDF thuần — ESP-IDF mạnh hơn về low-level nhưng đòi hỏi thời gian phát triển lâu hơn nhiều, chưa cần thiết ở giai đoạn hiện tại của dự án. |
| MQTT client (firmware) | **PubSubClient** | AsyncMqttClient, esp-mqtt (ESP-IDF native) | Payload nhỏ (JSON telemetry), gửi định kỳ, không cần xử lý concurrent nặng trên MCU nhỏ — `PubSubClient` đơn giản, nhẹ, tài liệu/cộng đồng lớn nhất trong hệ Arduino. `AsyncMqttClient` mạnh hơn về non-blocking nhưng phức tạp hoá code cho một nhu cầu chưa cần đến. |
| Đóng gói dữ liệu | **ArduinoJson** | cJSON, tự parse chuỗi thủ công | Chuẩn de facto cho JSON trên Arduino/ESP, API tiện, tiết kiệm RAM tốt hơn tự parse tay; `cJSON` (thư viện C thuần của ESP-IDF) chỉ hợp lý hơn nếu đã chuyển hẳn sang ESP-IDF native. |
| Hồng ngoại | **IRremoteESP8266** (chạy trên cả ESP32) | `IRremote` (Arduino gốc), tự bit-bang sóng mang 38kHz | Cần **thu raw đủ dài cho khung điều hoà** — remote máy lạnh gửi cả trạng thái (mode + nhiệt độ + quạt + hẹn giờ) trong một khung, dài gấp nhiều lần remote TV, nên buffer mặc định (~100 mốc) và ngưỡng "hết khung" 15ms của các thư viện phổ thông sẽ **cắt đôi khung** thành mã cụt. `IRremoteESP8266` sinh ra chính cho bài toán điều hoà: buffer 1024 mốc, `resultToRawArray()` bù sẵn `kMarkExcess`, `sendRaw()` phát lại nguyên văn. Tự bit-bang thì phải tự làm lại toàn bộ phần đó. |
| Cách điều khiển máy lạnh | **Phát lại raw đã học** | Decode/encode theo protocol từng hãng (`IRac`/`IRsend` chuyên dụng) | Phát lại đúng dạng sóng đã học chạy được với **mọi hãng, kể cả máy cũ/hàng nội địa** mà thư viện chưa hỗ trợ, và thêm hãng mới **không phải nạp lại firmware** (học qua app). Đánh đổi: node không "hiểu" trạng thái máy lạnh nên phải học đủ bộ COOL 24-28 + DRY/FAN/OFF trước khi auto-control chạy được. |
| Cache mã IR trên node | **NVS (`Preferences`)**, khoá băm FNV-1a từ `ir_code_id` | Gửi kèm `ir_raw` trong mọi lệnh, LittleFS | Backend cố tình chỉ gửi `ir_raw` **lần đầu** mỗi mã rồi dựa vào cache của node (`command_publisher._resolve_ir_raw`) — giữ payload MQTT nhỏ cho các lệnh sau. NVS nằm ở phân vùng riêng nên mã **sống qua cả `pio run -t upload`**. Phải băm vì NVS giới hạn tên khoá 15 ký tự mà `ir_code_id` là UUID 36 ký tự; lưu kèm UUID gốc để phát hiện đụng băm. |

---

## 5. Hạ tầng / Triển khai

| Lớp | Đã chọn | Phương án tương tự | Vì sao chọn cái này thay vì cái kia |
|---|---|---|---|
| Container | **Docker Compose** | Kubernetes, chạy trực tiếp (bare-metal/systemd) | Quy mô hiện tại là 1 VPS với vài container (Postgres, Redis, EMQX, api, worker, cloudflared) — Kubernetes over-engineering, tốn tài nguyên cho control-plane và độ phức tạp vận hành không cần thiết cho 1 node. Docker Compose đủ để cô lập service, dễ tái tạo môi trường dev giống prod, phù hợp với một người/nhóm nhỏ vận hành. Chạy trực tiếp không container hoá sẽ khó cô lập version dependency và khó rollback. |
| MQTT broker (production) | **EMQX** | Mosquitto (self-host), HiveMQ, VerneMQ | Production cần quản lý **auth/ACL theo từng thiết bị** (nhiều node của nhiều khách khác nhau chia sẻ 1 broker) — EMQX có dashboard quản trị, hỗ trợ auth/ACL linh hoạt và cả bản Serverless cloud managed giảm gánh vận hành. Mosquitto (đang dùng ở dev) nhẹ và nhanh để phát triển nhưng thiếu tooling quản lý ACL đa tổ chức mà production cần. HiveMQ/VerneMQ cũng mạnh nhưng không có lợi thế rõ rệt so với EMQX ở quy mô này, trong khi hệ sinh thái/tài liệu EMQX phổ biến hơn với Postgres/Redis stack đang dùng. |
| Ingress / TLS | **Cloudflare Tunnel** (HTTP/HTTPS) | Nginx reverse proxy + Let's Encrypt (mở port trực tiếp), VPN (WireGuard) | Cloudflare Tunnel phục vụ **web + app** mà không cần mở port HTTP nào, tự động TLS, miễn phí, dễ setup hơn tự quản lý renew chứng chỉ Let's Encrypt. **Đính chính 21/07/2026:** câu "0 cổng public ngoài SSH" **không còn đúng** — CF Tunnel chỉ chở được HTTP(S), không chở MQTT thô, nên **cổng 1883 đã phải mở trực tiếp ra internet** cho các node ESP kết nối. Đã bật **authentication per-device** và **topic ACL** trên EMQX (21/07/2026): mỗi node chỉ được publish/subscribe trong `bl/{org của chính nó}/#`, worker được `bl/#`. Khoá ở mức **org chứ không mức uuid** là cố ý — node master phải publish HỘ các slave lên topic riêng của slave (relay ESP-NOW), khoá cứng theo uuid sẽ chặn nhầm chính luồng đó. **Bẫy đã vấp và đã xử:** `acl.conf` mặc định kết thúc bằng `{allow, all}.` nên nguồn `file` cho qua mọi thứ mà `built_in_database` không khớp — `no_match=deny` không bao giờ tới lượt và ACL chỉ là trang trí. Phải **tắt nguồn `file`** thì luật mới thật sự cầm quyền. Đã kiểm chứng bằng cách dùng credential thật của một node thử publish sang org khác: bị chặn. Mở port + Nginx tự quản lý sẽ phải tự vá lỗ hổng và theo dõi cert hết hạn. VPN phù hợp cho truy cập nội bộ nhưng không tiện cho việc phục vụ traffic công khai từ khách hàng (app/web cần dùng được mà không phải cài VPN client). |

---

## 6. Nghiên cứu (`Research/`) & đề xuất bổ sung

Thư mục `Research/` gồm 3 file, xoay quanh **ASHRAE 55 – Adaptive Thermal Comfort** (đối
chiếu TCVN 5687:2024, TCVN 13521:2022):

1. **`Nhiệt Độ Thích Ứng ASHRAE 55.pdf`** — tài liệu nghiên cứu gốc.
2. **`Adaptive.html`** / **`Adaptive_V2.html`** — infographic đề xuất mô hình comfort **cá
   nhân hoá theo sinh trắc học** (giới tính, độ tuổi, BMI, vùng miền Bắc/Trung/Nam) và kiến
   trúc hệ thống thế hệ sau gồm:
   - **Cảm biến mm-Wave radar** — định vị người/tư thế (độ chính xác < 0.5 m), ước lượng
     tốc độ trao đổi chất (BMR) không cần thiết bị đeo.
   - **MAPPO** (Multi-Agent Proximal Policy Optimization — reinforcement learning) — tối ưu
     tần số máy nén + hướng gió, tuyên bố đạt ổn định nhiệt độ ±0.21 °C và tiết kiệm ~30.7%
     năng lượng so với điều khiển on/off thông thường.

Đây là hướng nghiên cứu thế hệ sau, **chưa được triển khai** trong code hiện tại — thuật
toán comfort hiện chỉ dùng running-mean + độ ẩm, cảm biến chỉ có DHT11/DHT22, chưa có thành
phần ML/RL nào.

### Đề xuất bổ sung (tương thích với nguyên tắc topology hiện tại)

| # | Hạng mục | Đề xuất | So sánh nhanh với phương án khác | Vị trí bổ sung |
|---|---|---|---|---|
| 1 | ~~**Node indoor (còn thiếu)**~~ → **ĐÃ LÀM** | `FirmWare/esp32-indoor/` — ESP32 DevKit V1 + `IRremoteESP8266`, phủ đủ 5 topic `telemetry`/`status`/`cmd`/`state`/`learn`, có học remote + cache mã IR trong NVS | Đúng như đề xuất là dùng `IRremoteESP8266` thay vì tự viết. **Khác đề xuất ở một điểm:** node nối WiFi/MQTT **trực tiếp**, KHÔNG làm ESP-NOW slave của master — lệnh `ir_raw` cỡ vài KB không lọt qua giới hạn 250 byte/gói của ESP-NOW (xem mục 2) | `FirmWare/esp32-indoor/` |
| 2 | ~~**Hoàn thiện ESP-NOW**~~ → **ĐÃ CHỐT KHÁC** | Giữ ESP-NOW **chỉ cho chiều outdoor → indoor** (gói 43 byte). Node indoor không làm slave mà kiêm luôn vai trò master, nối MQTT thẳng | Đề xuất ban đầu (chuyển *cả* indoor sang ESP-NOW slave) không khả thi: chiều cloud → indoor chở `ir_raw` vài KB, muốn qua ESP-NOW phải tự viết chia mảnh + ghép lại + báo thiếu mảnh — trong khi MQTT đã lo sẵn. ESP-NOW vẫn đúng cho outdoor vì gói chỉ 43 byte và một chiều | `esp32-indoor`, `esp8266-outdoor` |
| 2b | ~~**Dọn node ESP32-S3**~~ → **ĐÃ GIẢI QUYẾT KHÁC** (21/07/2026) | Không loại bo S3 nữa: `esp32-indoor/` nay có **2 build environment** (`esp32-devkit` và `esp32-s3`) dùng CHUNG một mã nguồn, khác nhau chỉ ở sơ đồ chân. Bo S3 đang chạy chính firmware đầy đủ này | Tránh chép phần IR sang thư mục thứ hai — hai bản sao cùng logic sẽ trôi xa nhau (dự án đã có vết như vậy, xem comment `web/static/app.js`). Cảnh báo "hai bo đá nhau khỏi broker" vẫn đúng nhưng chỉ khi chạy **đồng thời** hai bo cùng `DEVICE_UUID` — đã kiểm chứng trên broker: hiện chỉ 1 client indoor. Còn `MultipleResultsFound` thì không còn xảy ra (xem Đính chính đầu tài liệu) | `FirmWare/esp32-indoor/platformio.ini` |
| 3 | **Cảm biến mm-Wave radar** | Module radar (vd. Hi-Link LD2410/LD2450, Seeed MR60BHA2) trên node indoor để định vị người dùng | So với camera (vi phạm riêng tư, tốn xử lý ảnh) hoặc PIR thường (chỉ phát hiện chuyển động, không định vị/đo khoảng cách) — mm-Wave cho độ chính xác vị trí cao mà vẫn không quay video, phù hợp không gian phòng ngủ | Phần cứng node indoor; dữ liệu gửi kèm telemetry qua MQTT |
| 4 | **Thuật toán comfort cá nhân hoá** | Module tính target temp/humidity theo giới tính/tuổi/BMI/vùng miền (như `Adaptive.html`), **cộng thêm** vào engine hiện có chứ không thay thế | Giữ nguyên engine running-mean hiện tại làm baseline ổn định đã kiểm chứng, thêm lớp cá nhân hoá làm điều chỉnh biên — an toàn hơn thay thế toàn bộ bằng mô hình chưa kiểm chứng thực tế | `src/app/comfort/personalization.py` (mới) |
| 5 | **Điều khiển bằng RL (MAPPO)** | Engine điều khiển reinforcement learning làm lựa chọn thay thế rule-based, bật/tắt qua config flag, benchmark trước khi đưa vào production | Rule-based hiện tại dễ giải thích/debug cho một sản phẩm bán ra thị trường (khách hàng/nhà quản trị cần hiểu vì sao nhiệt độ đặt ra vậy) — RL là "hộp đen" hơn, nên triển khai song song và A/B test thay vì thay thế ngay | Module riêng trong `src/app/comfort/` |
| 6 | **Push notification** *(phụ, ngoài Research)* | `firebase_messaging` cho app Flutter | So với poll OTA hiện tại: push tiết kiệm pin/băng thông và báo tức thời hơn là chờ app tự kiểm tra định kỳ | `app-flutter/` |
| 7 | **CI/CD** *(phụ, ngoài Research)* | Pipeline build/test/deploy tự động (vd. GitHub Actions) | So với deploy thủ công qua `scripts/deploy.sh` hiện tại: giảm rủi ro thao tác tay, nhưng cần cân nhắc chi phí thời gian thiết lập so với tần suất deploy thực tế của một team nhỏ | Repo root |

---

<sub>Sinh ra và duy trì với sự hỗ trợ của Claude Code.</sub>
