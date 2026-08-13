# Pha 05 — App + web admin đa phòng

## Liên kết

- Tổng quan: [plan.md](plan.md) · Phụ thuộc: [pha 02](phase-02-trang-thai-va-comfort-theo-phong.md)
- `app-flutter/lib/state/app_state.dart`, `lib/screens/control/`, `lib/services/live_data_source.dart`
- `src/app/web/templates/`, `src/app/services/live_state_service.py`

## Tổng quan

**Ưu tiên:** Trung bình. **Trạng thái:** Chưa bắt đầu.

Cho người dùng chọn phòng trong app, và cho nhà quản trị quản lý node theo phòng
trên web.

## Nhận định quan trọng

**Vừa sửa xong đúng lớp code này, và bài học còn nóng.** `OverridePanel` từng giữ
trạng thái cục bộ không bao giờ đọc máy chủ (`late _mode = widget.initialMode`
chỉ chạy một lần). Đã sửa bằng `didUpdateWidget` + khoá "đang sửa" 5 giây. **Khi
thêm bộ chọn phòng, đừng tái lập lỗi đó theo chiều mới**: đổi phòng phải nạp lại
trạng thái phòng mới, không được giữ số của phòng cũ trên dial.

**Payload WS phải mang nhiều phòng.** `build_live_state` hiện trả một `comfort`,
một `indoor`, một `ac`. Đổi thành danh sách theo phòng — đây là **breaking change**
với app đang chạy (build 22/23), nên cần đường tương thích.

**Đã có sẵn `AcState`** (`models/ac_state.dart`) tách trạng thái thật khỏi khuyến
nghị `comfort.tSet`. Cấu trúc theo phòng dùng lại được nguyên.

## Yêu cầu

**Chức năng**
- App: chọn phòng; mỗi phòng có dial, số đo, ghi đè riêng.
- App hiện **số cảm biến còn sống** của phòng — người dùng biết số đo đến từ đâu.
- Web admin: tạo/sửa/xoá phòng, gán node vào phòng, học mã IR theo phòng.

**Phi chức năng**
- App build 22/23 đang chạy ngoài thực địa **không được vỡ** khi backend lên
  trước — payload cũ phải còn đọc được.

## Kiến trúc

Payload WS đổi hình:

```jsonc
{
  "type": "state",
  "outdoor": {"t": 31.2, "h": 70},        // dùng chung cả hộ
  "rooms": [
    {
      "id": "...", "name": "Phòng khách",
      "comfort": {...},                    // như ComfortPreview hiện tại
      "indoor": {"t": 25.4, "h": 62},      // ĐÃ GỘP từ N cảm biến
      "ac": {"mode": "COOL", "setpoint": 25},
      "sensors": {"alive": 3, "total": 4}
    }
  ],
  // tương thích ngược: lặp lại phòng ĐẦU TIÊN ở mức gốc cho app cũ
  "comfort": {...}, "indoor": {...}, "ac": {...}
}
```

Ba khoá gốc lặp lại là **cầu tạm**, bỏ sau khi phần lớn khách đã lên build mới.
Ghi hạn bỏ vào chú thích ngay tại chỗ, đừng để nó thành vĩnh viễn.

## File liên quan

**Sửa — backend**
- `live_state_service.py` — payload theo phòng + cầu tương thích
- `api/v1/comfort_routes.py`, `ir_routes` — nhận `room_id`
- `web/templates/` + `web/routes/` — quản lý phòng

**Sửa — app**
- `lib/models/` — thêm `room.dart`; `ac_state.dart` giữ nguyên
- `lib/state/app_state.dart` — danh sách phòng + phòng đang chọn
- `lib/screens/control/control_screen.dart` — bộ chọn phòng
- `lib/screens/dashboard/` — thẻ theo phòng

## Các bước

1. Backend: payload `rooms[]` + ba khoá gốc lặp lại phòng đầu.
2. App: model `Room`, `AppState.rooms` + `selectedRoomId` (nhớ trong `prefs`).
3. Bộ chọn phòng ở đầu tab ĐIỀU KHIỂN và TRẠNG THÁI.
4. **Đổi phòng = nạp lại trạng thái**: `OverridePanel` nhận `key: ValueKey(roomId)`
   để Flutter dựng State mới thay vì mang số phòng cũ sang.
5. Hiện `sensors.alive/total`; `alive == 0` thì nói thẳng "chưa có số đo", không
   hiện số cũ.
6. Web admin: trang phòng, gán node, học IR theo phòng.
7. Bump build, phát hành OTA.

## Todo

- [ ] Payload `rooms[]` + cầu tương thích ngược
- [ ] Model + state phòng trong app
- [ ] Bộ chọn phòng, nhớ lựa chọn
- [ ] `ValueKey(roomId)` cho `OverridePanel`
- [ ] Hiện số cảm biến còn sống
- [ ] Web admin quản lý phòng
- [ ] Thử app build 23 (cũ) với backend mới — không được vỡ

## Tiêu chí hoàn thành

- Hai phòng: đổi qua lại, dial hiện đúng số của từng phòng, không dính số cũ.
- Đặt 22 °C ở phòng A rồi sang phòng B: B không đổi.
- **App build 23 chưa cập nhật vẫn chạy** với backend mới (điều khiển phòng đầu).
- Rút hết cảm biến một phòng: app nói "chưa có số đo", không hiện số cũ.

## Rủi ro

| Rủi ro | Giảm thiểu |
|---|---|
| App cũ vỡ khi backend lên trước | Ba khoá gốc lặp lại; phải thử bằng APK build 23 thật |
| Dial giữ số phòng cũ khi đổi phòng | `ValueKey(roomId)` ép dựng State mới — chính lỗi vừa sửa, theo chiều khác |
| Payload phình to khi nhiều phòng | Chỉ gửi phòng có thay đổi nếu vượt ~10 phòng; chưa cần bây giờ |

## Bảo mật

- Mọi endpoint nhận `room_id` phải kiểm phòng thuộc org trong token.
- Không để `device_uuid` hay `mqtt_token` lọt vào payload app.

## Tiếp theo

Pha 06 (đổi tên) làm sau cùng để không phải sửa nhãn hai lần.
