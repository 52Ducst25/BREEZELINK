# Đa node theo phòng + AI giám sát trên Arduino UNO Q

Chuyển từ mô hình **1 hộ = 1 máy lạnh + 2 node** sang **1 hộ = N phòng, mỗi phòng
1 master (IR + WiFi) + N node cảm biến góc phòng**, kèm một cầu nối BLE đưa số đo
sang Arduino UNO Q để chạy AI giám sát/dự báo.

## Quyết định đã chốt

| Câu hỏi | Chốt |
|---|---|
| Uno Q nối bằng gì | **BLE (NimBLE)** từ room master |
| AI làm gì | **Giám sát / dự báo** — KHÔNG can thiệp vòng điều khiển |
| Phạm vi | **Nhiều phòng**, mỗi phòng 1 máy lạnh độc lập |

Hệ quả quan trọng của lựa chọn thứ hai: BLE **không nằm trên đường điều khiển**.
Nó phải được thiết kế tách rời và fail-safe — mất BLE thì máy lạnh vẫn chạy.

## Năm chốt chặn phải gỡ (theo thứ tự phụ thuộc)

1. **`ir_codes` khoá theo org** — `UniqueConstraint("org_id","mode","temp")`. Hai
   phòng hai máy khác hãng đụng nhau ngay tại đây. Nghiêm trọng nhất.
2. **Không có khái niệm phòng** — 0 model nào có `Room`; `Device.location` chỉ là
   chuỗi tự do, không nhóm được.
3. **Trạng thái lưu theo org** — `redis_key()` chỉ sinh `bl:{org}:{suffix}`, nên N
   node đo bốn góc **ghi đè số đo của nhau**.
4. **`comfort_engine` nhận đúng một `tin`/`hin`** — cần chính sách gộp N cảm biến.
5. **`NodeType` chỉ có `outdoor`/`indoor`** — thiếu loại node chỉ-cảm-biến.

## Các pha

| Pha | Nội dung | Trạng thái |
|---|---|---|
| [01](phase-01-mo-hinh-phong-va-di-tru.md) | Bảng `rooms`, `device.room_id`, đổi khoá `ir_codes`, `NodeType.sensor` | Chưa bắt đầu |
| [02](phase-02-trang-thai-va-comfort-theo-phong.md) | Redis theo phòng, gộp N cảm biến, comfort chạy độc lập từng phòng | Chưa bắt đầu |
| [03](phase-03-firmware-node-cam-bien.md) | Firmware node góc phòng (ESP-NOW, không WiFi/IR) | Chưa bắt đầu |
| [04](phase-04-ble-cau-noi-uno-q.md) | NimBLE trên room master, tách rời + fail-safe | Chưa bắt đầu |
| [05](phase-05-app-va-web-da-phong.md) | Chọn phòng trong app, web admin theo phòng | Chưa bắt đầu |
| [06](phase-06-doi-ten-room-master.md) | Đổi tên `indoor` → `room master`, cập nhật tài liệu | Chưa bắt đầu |

## Phụ thuộc

- Pha 01 chặn tất cả — không có `room_id` thì không pha nào làm được.
- Pha 02 chặn 05 (app cần API theo phòng).
- Pha 03 độc lập với 04; cả hai chỉ cần 01 xong.
- Pha 06 làm cuối để không phải đổi tên hai lần.

## Rủi ro lớn nhất

**Heap của room master.** Log boot hiện tại: tự do ~70 KB, thấp nhất từng chạm
63 KB. NimBLE cần ~30–40 KB. Biên còn lại rất mỏng, mà trên bo đã có LVGL + bộ
đệm MQTT 12 KB + `irBuf` 1.2 KB. Pha 04 phải **đo thật trước khi cam kết** và giữ
cờ biên dịch để tắt được BLE.

**Di trú dữ liệu.** Mọi hộ đang chạy đều có `ir_codes` khoá theo org và một node
`indoor`. Migration phải tạo một phòng mặc định và gán hết vào đó, nếu không mọi
hộ hiện có mất điều khiển ngay khi deploy.

## Câu chưa trả lời

- Gộp N cảm biến bằng cách nào: trung bình, nhỏ nhất, lớn nhất, hay trọng số theo
  góc? Ảnh hưởng trực tiếp tới cảm nhận người dùng. Pha 02 đề xuất mặc định là
  trung bình + loại giá trị lạc, cần bạn chốt.
- `configs` (tham số comfort) giữ theo hộ hay tách theo phòng? Kế hoạch hiện giữ
  theo hộ (YAGNI) — mỗi phòng vẫn dùng chung `deadband`, `night_offset`…
