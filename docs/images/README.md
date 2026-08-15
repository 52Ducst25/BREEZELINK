# Ảnh của dự án

Ảnh dùng trong tài liệu và bài thuyết trình: phần cứng lắp đặt, giao diện panel,
màn hình app, sơ đồ đấu dây, biểu đồ kết quả đo.

## Quy ước đặt tên

Tiếng Việt không dấu, kebab-case, **tả nội dung chứ không tả thiết bị chụp**:

```
phan-cung-lap-dat.png        ✅  đọc tên là biết trong ảnh có gì
panel-tab-may-tao-am.png     ✅
app-man-dieu-khien.png       ✅
20260727_104251.png          ❌  tên máy ảnh tự đặt, không nói gì
IMG_1234.jpg                 ❌
```

Lý do không phải thẩm mỹ: tài liệu nhúng ảnh bằng đường dẫn, và sáu tháng sau
không ai nhớ `20260727_104251.png` là ảnh gì để mà thay đúng cái cần thay.

## Nhúng vào tài liệu

Đường dẫn tương đối từ file `.md` đang viết:

```markdown
![Bộ sáu thiết bị sau khi lắp](images/phan-cung-lap-dat.png)
```

## Cỡ ảnh

Nén trước khi commit. Ảnh chụp điện thoại thường 2–5 MB mà hiển thị trong tài
liệu chỉ cần ~1600 px chiều ngang. Repo này đã một lần phình lên vì file nhị phân
không ai dùng tới, và git **không bao giờ quên** một file đã commit — xoá ở lần
sau cũng không làm nhẹ lịch sử.

```bash
# ImageMagick
magick input.jpg -resize 1600x -quality 82 docs/images/ten-mo-ta.png
```

## Đang có

| File | Nội dung |
|---|---|
| `phan-cung-lap-dat.png` | Ảnh chụp phần cứng lúc lắp đặt |
