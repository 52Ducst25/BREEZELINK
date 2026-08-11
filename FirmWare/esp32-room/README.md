# Node cảm biến góc phòng — ESP32-C3-DevKitM-1 + DHT22

Bốn bo giống hệt nhau đặt ở bốn góc một phòng. Mỗi bo đọc DHT22 rồi **bắn ESP-NOW**
về gateway đặt gần máy lạnh. Không WiFi, không MQTT, không bí mật nào trong `config.h`.

## Vì sao lại là bốn cảm biến

Một cảm biến treo trên tường không nói được nhiệt độ của phòng — nó nói nhiệt độ của
**cái tường đó**. Góc có nắng chiếu, góc dưới miệng gió điều hoà và góc sau tủ chênh
nhau 3–4 °C là chuyện thường.

Gateway và backend đều lấy **trung vị** các góc còn tươi (không phải trung bình cộng),
nên một góc bất thường không kéo được nhiệt độ đặt đi. Trung bình cộng thì kéo được —
vĩnh viễn, và triệu chứng duy nhất là "ở trong nhà thấy sai sai".

## Vì sao ESP-NOW chứ không phải Bluetooth

Gói ESP-NOW chở được 250 byte nên nó mang thẳng **`device_uuid` 32 ký tự của chính
node**. Gateway cứ thế publish vào topic của node đó — thêm hay bớt một góc chỉ cần
nạp bo mới, gateway không phải sửa gì và không phải nạp lại.

Gói BLE advertising cổ điển chỉ có 31 byte, chở không nổi uuid. Đi đường đó thì gateway
buộc phải giữ một mảng uuid theo thứ tự và nạp lại mỗi lần đổi node; lệch một ô là số đo
của góc A nộp lên cloud dưới tên góc B — biểu đồ vẫn có số, không lỗi ở đâu cả.

Bluetooth trong hệ này dành cho đường **gateway ↔ Arduino UNO Q**, nơi hai bên có kết
nối GATT thật (hai chiều, MTU thương lượng được) và không bị trần 31 byte.

## Nạp firmware

```bash
cd FirmWare/esp32-room
cp src/config.h.example src/config.h    # điền WIFI_SSID + DEVICE_UUID
pio run -e esp32c3-room -t upload --upload-port COMx
pio device monitor -p COMx -b 115200
```

`DEVICE_UUID` lấy ở web admin → **Khách hàng** → mở node *Cảm biến phòng* → mục
**Nạp firmware**. Mỗi góc là một hàng device riêng, nên **mỗi bo một uuid khác nhau**.

## Ba điều dễ mất thời gian nhất

- **`WIFI_SSID` phải giống hệt gateway và phải là băng 2.4 GHz.** Đây là chỗ hỏng câm
  số một. Node **không đăng nhập** WiFi — nó chỉ *quét* đúng chuỗi tên này để biết
  router đang ở kênh nào, vì ESP-NOW bắt buộc mọi bên cùng kênh. Gõ lệch một ký tự
  (hoặc điền tên băng 5 GHz) thì node bám kênh 1 mặc định, gói bay vào khoảng không,
  và vì broadcast **không có ACK** nên không một dòng log nào ở bất kỳ đâu báo lỗi.

  *Kiểm:* log lúc boot in ra tên mạng nó đang tìm và kênh nó bám được.

- **`ROOM_CORNER` chỉ là nhãn hiển thị.** Định danh thật là `DEVICE_UUID`. Hai bo trùng
  số góc là **vô hại**: cả hai vẫn có topic riêng, vẫn vào trung vị, chỉ là màn hình ghi
  nhãn trùng nhau. Đặt đúng thì người đi bảo trì đọc màn là biết ngay góc nào đang lệch.

- **Trở kéo 4.7k lên 3.3V trên đường dữ liệu DHT22.** Thiếu thì đọc được lúc được lúc
  không (checksum bắt được nên **không** ra số sai, chỉ NaN xen kẽ) — nhìn y hệt dây
  tuột. Và **nuôi DHT22 bằng 3.3V, không 5V**: chân ESP32-C3 không chịu quá áp.

## Sơ đồ chân

| Chân C3 | Nối tới | Ghi chú |
|---|---|---|
| GPIO4 | DATA của DHT22 | + trở kéo 4.7k lên 3.3V |
| 3V3 | VCC của DHT22 | **không** dùng 5V |
| GND | GND của DHT22 | |

Những chân **phải tránh** trên C3 và lý do: xem khối chú thích đầu
[platformio.ini](platformio.ini).

## Khuôn gói

45 byte, định nghĩa ở [`../shared/espnow-message.h`](../shared/espnow-message.h) — dùng
chung với node ngoài trời và gateway. Phần radio (quét kênh, bám kênh, bắn quảng bá) nằm
ở [`../shared/espnow-slave-radio.h`](../shared/espnow-slave-radio.h), cũng dùng chung với
node ngoài trời: cái bẫy "scanNetworks bỏ radio lại ở kênh cuối" đã trả giá một lần rồi,
không nên có hai bản sao của nó.

## Điện năng

Bản này chạy **nguồn USB 5V**. Muốn chạy pin thì phải thêm deep-sleep — ESP-NOW rất hợp
với việc đó vì không có bắt tay WiFi/DHCP/TCP nào phải làm lại sau mỗi lần thức, khác hẳn
một node nối WiFi thật. Chưa làm.
