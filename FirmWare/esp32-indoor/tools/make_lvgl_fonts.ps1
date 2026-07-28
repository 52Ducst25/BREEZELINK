# ============================================================================
#  Sinh font LVGL có ĐỦ DẤU TIẾNG VIỆT, xuất ra src/ui/fonts/aircon_*.c
# ----------------------------------------------------------------------------
#  Chạy:  powershell -ExecutionPolicy Bypass -File tools\make_lvgl_fonts.ps1
#  Cần:   npm install -g lv_font_conv
#
#  VÌ SAO KHÔNG DÙNG MONTSERRAT DỰNG SẴN CỦA LVGL: nó chỉ có ASCII + ° + •, nên
#  "LÀM LẠNH" ra ô vuông hoặc mất dấu. Đây là công cụ chính thức của LVGL, sinh
#  ra mảng C nhúng thẳng vào flash — không cần SPIFFS, không cần bước nạp font
#  riêng lúc sản xuất.
#
#  QUAN HỆ VỚI make_vlw.py: file đó sinh font cho TFT_eSPI (định dạng VLW), dùng
#  cho bản giao diện cũ vẽ trực tiếp. Bản hiện tại vẽ bằng LVGL nên dùng script
#  này. Giữ cả hai để còn quay lại được; ĐỪNG sửa một cái mà quên cái kia nếu
#  đổi bộ ký tự.
#
#  HAI NHÓM FONT, khác nhau về chủ đích — không phải tuỳ tiện:
#    viet_* : đủ dải 0x1EA0-0x1EF9 (toàn bộ tổ hợp dấu tiếng Việt), 2 bpp.
#             2 bpp thay vì 4 để cắt nửa dung lượng; trên màn 2.8" ở khoảng cách
#             đứng mắt không phân biệt được, mà tiết kiệm ~70 KB flash.
#    num_*  : CHỈ chữ số + ° C % + - . Nhiệt độ và setpoint không bao giờ chứa
#             chữ tiếng Việt; nhúng cả bảng dấu vào cỡ 40 px là phí ~90 KB.
#             4 bpp vì số lớn nằm ngay tầm mắt, răng cưa lộ rõ.
# ============================================================================
$ErrorActionPreference = 'Stop'

$fontDir = Join-Path $PSScriptRoot '..\src\ui\fonts'
$fontDir = [System.IO.Path]::GetFullPath($fontDir)
New-Item -ItemType Directory -Force -Path $fontDir | Out-Null

# Dải ký tự tiếng Việt. Precomposed (dựng sẵn) chứ không phải tổ hợp dấu rời:
#   0x20-0x7F  ASCII
#   0xB0       ° (độ)          |  0x2022  • (chấm giữa — U+00B7 KHÔNG có trong font)
#   0xC0-0xFF  À Á Â Ã È É Ê Ì Í Ò Ó Ô Õ Ù Ú Ý và bản thường
#   0x102-3    Ă ă   |  0x110-1  Đ đ   |  0x128-9  Ĩ ĩ   |  0x168-9  Ũ ũ
#   0x1A0-1    Ơ ơ   |  0x1AF-B0 Ư ư
#   0x1EA0-EF9 toàn bộ tổ hợp dấu hỏi/ngã/nặng — phần LỚN NHẤT và không thể bỏ
$viet = @(
  '-r','0x20-0x7F','-r','0xB0','-r','0xC0-0xFF','-r','0x102-0x103','-r','0x110-0x111',
  '-r','0x128-0x129','-r','0x168-0x169','-r','0x1A0-0x1A1','-r','0x1AF-0x1B0',
  '-r','0x1EA0-0x1EF9','-r','0x2022'
)
$num = @('-r','0x20','-r','0x25','-r','0x2B','-r','0x2D-0x2E','-r','0x30-0x39','-r','0x43','-r','0xB0')

# Arial có sẵn trên Windows và phủ đủ tiếng Việt. Đổi font khác thì kiểm tra nó
# có glyph cho dải 0x1EA0-0x1EF9 — thiếu là chữ ra ô vuông, và lv_font_conv
# KHÔNG báo lỗi, nó chỉ lặng lẽ bỏ qua ký tự thiếu.
$jobs = @(
  @{ name='aircon_viet_12'; ttf='arial.ttf';   size=12; bpp=2; ranges=$viet },
  @{ name='aircon_viet_16'; ttf='arialbd.ttf'; size=16; bpp=2; ranges=$viet },
  @{ name='aircon_num_28';  ttf='arialbd.ttf'; size=28; bpp=4; ranges=$num  },
  @{ name='aircon_num_40';  ttf='arialbd.ttf'; size=40; bpp=4; ranges=$num  }
)

foreach ($j in $jobs) {
  $out = Join-Path $fontDir "$($j.name).c"
  $ttf = Join-Path $env:WINDIR "Fonts\$($j.ttf)"
  if (-not (Test-Path $ttf)) { throw "Khong thay font nguon: $ttf" }

  # --no-compress BẮT BUỘC. lv_font_conv mặc định NÉN khi bpp > 1, còn lv_conf.h
  # để LV_USE_FONT_COMPRESSED = 0 -> LVGL không giải nén nổi và BỎ QUA TỪNG CHỮ
  # MỘT. Trên màn thì thành "chạy được nhưng thiếu chữ, chạm vào nhấp nháy",
  # nhìn y như hỏng driver màn hình hay sụt nguồn. Đã mất một vòng nạp firmware
  # đi tìm nhầm sang phía bộ nhớ vì cái này.
  #
  # Chọn BỎ NÉN thay vì bật LV_USE_FONT_COMPRESSED: LVGL không có bộ nhớ đệm
  # glyph, nên bật nén là giải nén lại từng chữ ở MỖI LẦN VẼ. Đổi ~60 KB flash
  # (còn dư hơn 1.8 MB) lấy việc khỏi tốn CPU mãi mãi là đổi đáng.
  $a = @('--font',$ttf,'--size',$j.size,'--bpp',$j.bpp,'--format','lvgl','--no-compress') +
       $j.ranges + @('--force-fast-kern-format','-o',$out)
  & lv_font_conv.cmd @a
  if (-not (Test-Path $out)) { throw "Sinh font that bai: $($j.name)" }
  '{0,-16} {1,7} KB' -f $j.name, [math]::Round((Get-Item $out).Length / 1KB, 1)
}

Write-Host ''
Write-Host 'Xong. Nho khai LV_FONT_DECLARE trong src/ui/theme.h — NGOAI namespace,'
Write-Host 'dat trong namespace thi ten bi mangle va chi dut o buoc LIEN KET.'
