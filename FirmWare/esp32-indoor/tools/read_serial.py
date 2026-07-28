# ============================================================================
#  Đọc log serial trong N giây rồi thoát — KHÔNG reset bo.
# ----------------------------------------------------------------------------
#  Chạy:  python tools\read_serial.py COM11 20
#
#  VÌ SAO KHÔNG DÙNG `pio device monitor`: nó bật RTS/DTR khi mở cổng, mà hai
#  chân đó nối vào EN/BOOT của ESP32 -> vừa mở log là bo KHỞI ĐỘNG LẠI. Lỗi nào
#  cần nhìn trạng thái tích luỹ (mất nhịp tim, treo sau vài phút) thì mở log
#  chính là XOÁ MẤT BẰNG CHỨNG. Đã dính đúng bẫy này một lần trong dự án khi
#  truy lỗi ESP-NOW.
#
#  Đặt rtscts/dsrdtr=False TRƯỚC khi mở, rồi mới gán mức — thứ tự ngược lại thì
#  pyserial vẫn nhá chân một nhịp lúc mở.
# ============================================================================
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM11"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

s = serial.Serial()
s.port = port
s.baudrate = 115200
s.rtscts = False
s.dsrdtr = False
s.timeout = 0.2
s.open()
s.rts = False
s.dtr = False

end = time.time() + secs
buf = b""
while time.time() < end:
    buf += s.read(4096)
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        print(line.decode("utf-8", "replace").rstrip())
        sys.stdout.flush()
s.close()
