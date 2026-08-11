# Edge AI — Arduino UNO Q

Dịch vụ Python chạy trên **nửa Linux** của Arduino UNO Q (Debian trên Qualcomm
Dragonwing QRB2210). Nửa MCU (STM32U585) không tham gia luồng này — "edge AI" ở
đây là một dịch vụ trên máy tính nhỏ, không phải một sketch trên vi điều khiển.

## Nó làm gì

1. Nối gateway qua **Bluetooth (GATT)** — UNO Q là *central*, gateway là *peripheral*.
2. Nghe ảnh chụp gateway đẩy sang mỗi 5 giây: 4 góc phòng, ngoài trời, trạng thái máy
   lạnh, và **máy chủ đã im lặng bao lâu**.
3. Giữ lịch sử **từng góc** (30 phút), dự báo nhiệt độ 15 phút tới, phát hiện góc
   bất thường.
4. Tính nhiệt độ đặt bằng **chính thuật toán của backend** (`src/app/comfort/`).
5. Bình thường: chỉ **đề xuất** (`kind=ADVICE`) — gateway ghi nhật ký, KHÔNG bắn IR.
6. Gateway báo cloud im quá `EDGE_TAKEOVER_AFTER_SEC`: gửi `kind=COMMAND` → gateway bắn IR.

## Vì sao Bluetooth chứ không phải MQTT

**Lớp dự phòng phải sống sót đúng cái sự cố nó sinh ra để chịu đựng.** Bản đầu của dịch
vụ này nói chuyện với hệ qua MQTT — nghĩa là khi mất mạng, đúng lúc cần nó nhất, nó cũng
mất luôn đường tới gateway và không cứu được gì. BLE là liên kết trực tiếp giữa hai thiết
bị đặt cùng phòng: không router, không internet, không broker.

Đổi này còn xoá sạch một lớp lỗi. Bản MQTT subscribe đúng topic nó publish, nên lệnh
giành lái của chính nó vọng về và bị đọc thành "cloud sống lại" → nhả lái một nhịp sau
khi giành, mãi mãi, 30 giây một lần. Nay **gateway** đếm sự im lặng, và nó chỉ đếm lệnh
của **máy chủ** — không còn tiếng vọng nào để nhầm.

Hệ quả phụ đáng giá: dịch vụ này **không cần credential MQTT nào cả**.

## Vì sao nó không tự lái ngay từ đầu

Cloud và UNO Q cùng ra lệnh là máy lạnh nhận hai lệnh trái nhau cách nhau một phút.
Triệu chứng — nhiệt độ đặt tự nhảy — trông y hệt lỗi thuật toán, và nó đẩy người đi
truy lỗi sang đúng nửa sai của hệ thống.

Nên luật là **bất đối xứng có chủ đích**:

| | điều kiện |
|---|---|
| **Giành lái** | gateway báo cloud im lặng **rất lâu** (mặc định 300s ≈ 20 nhịp telemetry) |
| **Nhả lái** | ngay ảnh chụp đầu tiên báo cloud đã lên tiếng |

Và một ranh giới nữa, ở tầng giao thức: **`kind=ADVICE` là mặc định ở mọi đường**.
Gateway chỉ bắn hồng ngoại khi nhận `kind=COMMAND`, nên mọi nhánh không đủ điều kiện đều
rơi về "chỉ đề xuất" một cách an toàn thay vì phải nhớ chặn.

Giành lái chậm thì mất vài phút không thích ứng. Nhả lái chậm thì hai bên giành
máy nén. Hai cái giá đó không bằng nhau.

## Vì sao nó không chép lại thuật toán comfort

`src/app/comfort/` là mã **thuần hàm** — không DB, không Redis, không MQTT — nên
import và chạy nguyên vẹn trên UNO Q ([`comfort_bridge.py`](edge_ai/comfort_bridge.py)).
Viết lại ở đây sẽ tạo ra **hai câu trả lời** cho "nhà này nên bao nhiêu độ", lệch
dần theo mỗi lần sửa backend, và triệu chứng của việc lệch đó là một cái máy lạnh
chạy sai nhiệt độ trong nhà người ta.

Ngoại lệ duy nhất là 11 hằng số cấu hình: import chúng sẽ kéo theo SQLAlchemy và
toàn bộ ORM — quá nặng cho một thiết bị ngoài hiện trường. Chúng được chép vào
`controller._FALLBACK_CFG` kèm chú thích trỏ về nguồn, và **hộ nào đã tinh chỉnh
thuật toán trên web thì phải dán cấu hình thật vào `EDGE_COMFORT_CONFIG`** —
không thì edge tính lệch với cloud mà không bên nào báo lỗi.

## Vì sao chỉ có một phụ thuộc

`bleak`, hết. Đây là phần mềm chạy trên thiết bị ở nhà khách: mỗi gói thêm vào là một
thứ nữa có thể vỡ lúc nâng cấp và một thứ nữa phải vá khi có CVE.

Bố cục gói BLE đọc bằng `struct` của thư viện chuẩn. Dự báo dùng hồi quy tuyến tính
bình phương tối thiểu viết tay — nhiệt độ phòng trong 15-30 phút gần như tuyến tính, và
một mô hình học sâu sẽ cần dữ liệu gán nhãn mà dự án không có, một quy trình huấn luyện
không ai bảo trì, và khó giải thích hơn hẳn khi khách hỏi "sao nó lại bật máy nén".

`predictor.py` cố ý để giao diện hẹp để sau này thay ruột bằng mô hình nặng hơn
mà không phải đụng vào `controller.py`.

## Cài đặt

Cần một adapter Bluetooth đang chạy (UNO Q có sẵn) và BlueZ.

```bash
# trên UNO Q (Debian)
sudo useradd -r -s /usr/sbin/nologin breezelink
sudo mkdir -p /opt/breezelink && sudo chown -R breezelink /opt/breezelink

# cần CẢ src/ của backend (edge import app/comfort từ đó)
sudo rsync -a --exclude '.pio' /duong/dan/repo/src /duong/dan/repo/edge-ai /opt/breezelink/

cd /opt/breezelink/edge-ai
python3 -m venv .venv && .venv/bin/pip install -e .
cp .env.example .env && sudo -e .env          # chỉ cần EDGE_ORG_ID

sudo cp deploy/breezelink-edge-ai.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now breezelink-edge-ai
journalctl -u breezelink-edge-ai -f
```

## Chạy thử trên máy dev

```bash
cd edge-ai
pip install -e .
cp .env.example .env    # điền
python -m edge_ai.main
```

## Kiểm chứng hoạt động đúng

| Việc làm | Phải thấy |
|---|---|
| Bật lên, cloud đang chạy | `Đã nối gateway … (MTU 247)`, rồi mỗi 30s một dòng `t_in=… máy chủ cầm lái`. KHÔNG có `ĐÃ RA LỆNH` |
| Xem màn gateway, trang Thông tin | Chân trang hiện `UNO Q đã nối` |
| Tắt worker cloud | Sau ~300s: `GIÀNH LÁI`, rồi `ĐÃ RA LỆNH (edge cầm lái)`; nhật ký trên màn gateway ghi `edge takeover` |
| Bật lại worker cloud | `NHẢ LÁI` ngay nhịp kế tiếp, thôi ra lệnh |
| Rút điện cả 4 node góc | `Gateway báo chưa có góc phòng nào còn tươi — không tính, không ra lệnh` |
| Che một góc bằng đèn bàn | `Bất thường ở góc 3 (outlier): lệch +3.2°C…` |
| Bấm THỦ CÔNG trên màn gateway | edge thôi ra lệnh dù đang cầm lái |

## Những chỗ dễ sai

- **`EDGE_ORG_ID` sai là hỏng câm một nửa.** Giá trị này băm thành `link_key`; sai thì
  dịch vụ vẫn nối được gateway và vẫn **nghe** được số đo, nhưng mọi lệnh gửi đi bị
  gateway lặng lẽ từ chối. Dấu hiệu duy nhất nằm ở log gateway:
  `[unoq] tu choi goi sai link_key`.
- **MTU.** Ảnh chụp là 39 byte còn MTU mặc định của BLE chỉ cho 20. Cả hai bên đều kiểm
  và kêu to, nhưng nếu thấy `Bỏ ảnh chụp không hợp lệ: … cắt cụt` lặp lại thì đó là
  BlueZ không thương lượng MTU lên được.
- **Đổi khuôn gói phải đổi CẢ HAI bên** — `edge_ai/protocol.py` và
  `FirmWare/shared/unoq-link-protocol.h`. Kích thước được chốt bằng `assert` lúc import
  và `static_assert` lúc biên dịch, nên quên là nổ ngay chứ không âm thầm đọc lệch.
- **UNO Q mất điện là mất lớp dự phòng.** Nó là lớp THÊM, không phải đường sống duy
  nhất — hệ vẫn chạy y như trước khi có nó.

## Giới hạn đã biết

- Edge chỉ biết hộ đã học những nhiệt độ nào bằng cách **quan sát trạng thái gateway
  báo về**. Một hộ vừa lắp mà cloud chưa kịp ra lệnh COOL lần nào thì edge sẽ chỉ đề
  xuất, không điều khiển — và nói ra điều đó trong log.
- Lệnh do edge phát **không tạo hàng `commands`** nào bên server (server đang mất kết
  nối — đó là lý do edge cầm lái). Gateway vẫn publish `state` nếu còn MQTT, nên web
  thấy trạng thái mới nhưng không thấy lệnh nào sinh ra nó. Nhật ký trên màn gateway
  ghi `edge takeover` — đó là chỗ duy nhất giải thích.
- **Chưa chạy trên phần cứng thật.** Toàn bộ luồng quyết định đã kiểm bằng smoke test
  với gói dựng đúng byte, nhưng kết nối BLE thật thì chưa.
