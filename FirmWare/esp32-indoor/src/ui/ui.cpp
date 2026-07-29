#include "ui.h"
#include "ui-screens.h"
#include "theme.h"
#include "touch.h"
#include "board-io.h"
#include "../config.h"

#include <lvgl.h>
#include <TFT_eSPI.h>

// ============================================================================
//  Cổng nối LVGL với phần cứng + tác vụ giao diện trên lõi 0.
// ----------------------------------------------------------------------------
//  VÌ SAO CHUYỂN SANG LVGL (bản trước vẽ thẳng bằng TFT_eSPI):
//
//  1. VẼ LẠI TỪNG PHẦN LÀ VIỆC CỦA THƯ VIỆN. Bản trước phải tự giữ một struct
//     `Drawn` chép lại mọi giá trị "đã vẽ lần trước" rồi so tay để biết ô nào
//     cần tô lại — mỗi lần thêm một trường là thêm một chỗ có thể quên so.
//     LVGL tự đánh dấu vùng bẩn và chỉ vẽ đúng vùng đó.
//  2. CHẠM LÀ VIỆC CỦA THƯ VIỆN. Chống dội, phân biệt nhấn/thả, tìm đúng đối
//     tượng dưới ngón tay — bản trước làm tay bằng bảng chữ nhật + khoá 250 ms.
//  3. BỐ CỤC SỐNG TRONG CÂY WIDGET, không nằm rải trong các lệnh vẽ. Đổi thiết
//     kế là đổi chỗ dựng, không phải dò lại hàng chục toạ độ trong hàm vẽ.
//
//  KIẾN TRÚC HAI LÕI GIỮ NGUYÊN — xem ui.h. LVGL **không an toàn đa luồng**,
//  nên mọi lời gọi lv_* chỉ được xảy ra trong tác vụ này.
// ============================================================================
namespace Ui {
namespace {

TFT_eSPI tft;

// ============================================================================
//  HAI CÔNG TẮC CHỈNH MÀN — đọc kỹ trước khi đổi
// ----------------------------------------------------------------------------
//  DISPLAY_INVERT: tấm này có cần đảo màu không. Đặt sai thì TOÀN BỘ giao diện
//  ra âm bản (nền đen thành trắng, xanh nhấn thành cam) mà bố cục vẫn đúng —
//  nên trông như chọn sai bảng màu chứ không phải lỗi phần cứng.
//
//  DISPLAY_SELFTEST: hiện 3 ô màu có ghi tên trong 3 giây lúc khởi động. Bật
//  lên khi nghi ngờ màu; nhìn 3 giây là biết ngay chiều đảo nào đúng, thay vì
//  nạp lại firmware nhiều lần để đoán. Chốt xong thì đặt về 0.
// ============================================================================
#define DISPLAY_INVERT   0
#define DISPLAY_SELFTEST 1

/// Hiện 3 ô màu có ghi tên. Vẽ THẲNG bằng TFT_eSPI, không qua LVGL — nên nó
/// kiểm tra được cả đường dẫn phần cứng lẫn chiều đảo màu, độc lập với mọi thứ
/// tôi viết ở tầng giao diện.
void colorSelfTest() {
  struct Swatch { uint8_t r, g, b; const char *name; };
  static const Swatch kSw[3] = {
      {0x00, 0x55, 0xFF, "XANH DUONG"},   // đảo sai -> ra CAM
      {0xE7, 0xF1, 0xF8, "TRANG"},        // đảo sai -> ra ĐEN
      {0x22, 0xC5, 0x5E, "XANH LA"},      // đảo sai -> ra HONG/TIM
  };

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(8, 8);
  tft.print("KIEM TRA MAU - ban PHAI thay:");

  for (int i = 0; i < 3; i++) {
    const int x = 8 + i * 104;
    tft.fillRect(x, 30, 96, 80, tft.color565(kSw[i].r, kSw[i].g, kSw[i].b));
    tft.setCursor(x, 118);
    tft.print(kSw[i].name);
  }

  tft.setCursor(8, 150);
  tft.print("Neu thay CAM / DEN / HONG -> dao mau nguoc,");
  tft.setCursor(8, 164);
  tft.print("doi DISPLAY_INVERT trong src/ui/ui.cpp.");
  tft.setCursor(8, 192);
  tft.printf("Dang chay: DISPLAY_INVERT = %d", DISPLAY_INVERT);

  Serial.printf("Kiem tra mau: DISPLAY_INVERT=%d — 3 o phai la "
                "XANH DUONG / TRANG / XANH LA\n", DISPLAY_INVERT);
  delay(3000);
}

// --- Bộ đệm vẽ ---------------------------------------------------------------
//  Hai bộ đệm 320×48 = 30.7 KB mỗi cái, CẤP PHÁT LÚC CHẠY.
//
//  TRƯỚC LÀ MẢNG TĨNH 320×20. Đổi vì hai lý do, cả hai đều đo được:
//
//  1. KHÔNG CÒN CHỖ TĨNH. Đoạn .bss của ESP32 chặn ở ~160 KB và bản cũ đã gần
//     kịch (nâng LV_MEM_SIZE lên 64 KB là đứt liên kết ngay). Heap lúc chạy thì
//     còn 171 KB — đó mới là chỗ để lấy.
//
//  2. ÍT LƯỢT ĐẨY = MƯỢT HƠN. Mỗi lượt flush tốn phí cố định: setAddrWindow,
//     bật/tắt CS, dựng lệnh SPI. 20 dòng thì vẽ hết màn mất 12 lượt; 48 dòng
//     còn 5. Đó là phần "khựng" mắt nhìn thấy khi đổi tab, chứ không phải tốc
//     độ SPI.
//
//  MALLOC_CAP_DMA là BẮT BUỘC, không phải cho chắc: pushPixelsDMA đọc thẳng từ
//  vùng nhớ này, mà bộ điều khiển DMA của ESP32 chỉ với tới được RAM trong. Cấp
//  bằng malloc() thường thì vẫn chạy... cho tới hôm nào nó rơi vào vùng khác.
constexpr uint32_t kBufLines = 48;
constexpr size_t   kBufPx    = Theme::SCREEN_W * kBufLines;
lv_disp_draw_buf_t drawBuf;
lv_color_t *buf1 = nullptr;
lv_color_t *buf2 = nullptr;
lv_disp_drv_t  dispDrv;
lv_indev_drv_t indevDrv;

/// initDMA() có thành công không. KHÔNG được mặc định là true.
///
/// Bo này chạy SPI qua ma trận GPIO (chân 18/22/19/21/23 không phải chân VSPI
/// mặc định), nên DMA có thể không dựng được. Mà pushPixelsDMA của TFT_eSPI mở
/// đầu bằng:
///     if ((len == 0) || (!DMA_Enabled)) return;
/// — tức là nó KHÔNG VẼ GÌ và KHÔNG BÁO GÌ. Màn giữ nguyên màu fillScreen lúc
/// khởi động, nhìn y như màn chết hoặc đèn nền hỏng. Đã mất một vòng nạp vì cái
/// này, nên phải hỏi lại kết quả và có đường lui.
bool gDma = false;

void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  // Byte đã đảo sẵn nhờ LV_COLOR_16_SWAP=1 trong lv_conf.h -> không đảo lần nữa.
  // Đặt lệch cặp này thì bố cục vẫn đúng mà MÀU SAI (xanh ra cam), trông như
  // hỏng phần cứng nên rất dễ đi tìm nhầm chỗ.
  if (gDma) {
    // DMA giao việc nạp từng từ vào thanh ghi SPI cho phần cứng, CPU rảnh ra.
    tft.pushPixelsDMA(reinterpret_cast<uint16_t *>(px), w * h);
    // dmaWait() BẮT BUỘC, và đây là chỗ dễ sai nhất của cả file này:
    // endWrite() của TFT_eSPI KHÔNG chờ DMA — nó kéo CS lên ngay. Thiếu dòng
    // dưới thì mọi lượt truyền bị cắt giữa chừng, đồng thời lv_disp_flush_ready
    // cho phép LVGL ghi đè bộ đệm đang truyền dở. Kết quả: màn KHÔNG hiện gì,
    // giữ nguyên màu fillScreen — nhìn hệt như đèn nền hỏng hoặc màn chết, mà
    // log thì sạch bong vì không có gì báo lỗi cả.
    tft.dmaWait();
  } else {
    tft.pushColors(reinterpret_cast<uint16_t *>(px), w * h, false);
  }
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

// --- Độ sáng -----------------------------------------------------------------
//  ĐÃ BỎ HẲN TỰ HẠ SÁNG. Bản đầu tụt xuống 15% sau 60 giây rồi nhảy phắt lên
//  khi chạm — người dùng đọc ra là "màn cứ bị tối, chạm vào thì nhấp nháy", và
//  nó giống hỏng phần cứng đủ để tôi đi tìm lỗi nhầm qua mấy vòng nạp firmware:
//  driver màn, DMA, bộ nhớ LVGL. Không cái nào có lỗi cả.
//
//  Bài học đáng giữ: một TÍNH NĂNG tự động, nếu người dùng không đoán được nó
//  sắp làm gì, sẽ bị báo lại như một LỖI PHẦN CỨNG. Panel treo tường luôn sáng
//  hết cỡ; muốn tối thì vào CÀI ĐẶT chỉnh — chủ động, và đoán trước được.
//
//  Giữ lại phần chuyển dần: chỉnh trong Cài đặt sẽ trôi mượt thay vì giật nấc,
//  và lúc khởi động màn sáng dần lên thay vì loé một cái.
constexpr uint8_t kBlStep = 2;   // %/nhịp; nhịp 5 ms -> toàn dải ~250 ms

uint8_t gBrightness = 100;   // mức người dùng đặt; mặc định SÁNG HẾT
uint8_t gBlTarget   = 0;     // mức đang hướng tới
uint8_t gBlNow      = 0;     // mức đang thực sự phát ra

/// Đặt ĐÍCH độ sáng. Không ghi thẳng vào phần cứng — blTick() bò dần tới đích.
void blSet(uint8_t percent) { gBlTarget = percent; }

/// Gọi mỗi nhịp tác vụ UI. Chưa tới đích thì tiến một bước.
void blTick() {
  if (gBlNow == gBlTarget) return;
  if (gBlNow < gBlTarget) {
    gBlNow = (uint8_t)((gBlTarget - gBlNow > kBlStep) ? gBlNow + kBlStep : gBlTarget);
  } else {
    gBlNow = (uint8_t)((gBlNow - gBlTarget > kBlStep) ? gBlNow - kBlStep : gBlTarget);
  }
  BoardIo::backlightSet(gBlNow);
}

void touchCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  int16_t x = 0, y = 0;
  static bool wasDown = false;

  if (Touch::read(x, y)) {
    // In MỘT dòng cho mỗi lần đặt ngón tay (không in suốt lúc giữ, vì hàm này
    // chạy 15 ms một lần). Cần để dò lệch toạ độ: bấm vào tab ngoài cùng bên
    // phải mà log ra x=250 trong khi tab đó nằm ở 240-320 thì biết ngay là
    // thang đo bị co, chứ không phải LVGL bỏ sót sự kiện.
    if (!wasDown) {
      wasDown = true;
      Serial.printf("cham: x=%d y=%d\n", (int)x, (int)y);
    }
    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PRESSED;
  } else {
    wasDown     = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// --- Ranh giới giữa hai tác vụ ----------------------------------------------
//  Ngoài bốn thứ dưới đây, hai lõi không đụng chung biến nào.
SemaphoreHandle_t modelMx = nullptr;   // bảo vệ `shared`
Model             shared;              // loop() ghi, tác vụ UI đọc
QueueHandle_t     cmdQ    = nullptr;   // UI    -> loop(): người dùng vừa bấm
QueueHandle_t     replyQ  = nullptr;   // loop() -> UI   : kết quả, hiện thành toast
TaskHandle_t      uiTask  = nullptr;

struct Reply { char msg[40]; };

// Số đo SHT3x: tác vụ UI ghi (nó sở hữu bus I2C), loop() đọc để gửi telemetry.
// Spinlock chứ không mutex: chỉ chép hai float, ngắn hơn cả chi phí đổi tác vụ.
portMUX_TYPE indoorMux = portMUX_INITIALIZER_UNLOCKED;
float gIndoorT = NAN, gIndoorH = NAN;

constexpr BaseType_t  kUiCore  = 0;      // loop() ở lõi 1; IR bit-bang không chịu bị xen
// 10240 là con số của bản CHƯA CÓ ẢNH. Đường vẽ giờ có thêm giải mã ảnh, nhuộm
// màu ảnh có alpha, và hai callback vẽ tay (tam giác + đường chéo cho góc vát) —
// đều ăn stack, và chúng lồng vào nhau trong cùng một lần vẽ. Tràn stack ở
// FreeRTOS không báo lỗi biên dịch, nó chỉ làm bo RESET, nên đừng tiết kiệm ở
// đây. Mức dùng thật in ra 15 giây một lần trong uiTaskFn — cứ nhìn số mà chỉnh.
constexpr uint32_t    kUiStack = 16384;
constexpr UBaseType_t kUiPrio  = 2;      // trên loopTask (1), dưới ngăn xếp WiFi (18+)
constexpr uint32_t    kDrawMs   = 200;   // nhịp đổ số liệu; mắt không phân biệt nhanh hơn
constexpr uint32_t    kSensorMs = 2000;  // SHT3x chuyển đổi ~20 ms, đo dày hơn là vô ích
constexpr uint32_t    kMemLogMs = 15000; // đo heap LVGL; đủ thưa để không rác log

// --- Cầu nối từ màn hình về loop() -------------------------------------------
// KHÔNG gọi beep() ở đây nữa. Tiếng bấm giờ do Theme::pressFeedback() phát,
// gắn vào MỌI nút — kể cả tab, +/−, và các nút trong Cài đặt, vốn trước đây im
// lặng. Để lại beep() ở đây là kêu hai tiếng chồng nhau.
//
// Đổi chỗ này cũng sửa luôn một lỗi: trước kia beep() chạy TRƯỚC buzzerEnable()
// trong nhánh tắt còi, nên bấm TẮT là còi kêu mãi không dừng (xem chú thích
// trong board-io.cpp).
void onCommand(const Command &c) {
  if (cmdQ) xQueueSend(cmdQ, &c, 0);
}

void onSetting(Screens::Setting s) {
  switch (s) {
    case Screens::BRIGHT_DOWN:
      gBrightness = (gBrightness > 20) ? gBrightness - 10 : 10;
      blSet(gBrightness);
      Screens::setBrightness(gBrightness);
      break;
    case Screens::BRIGHT_UP:
      gBrightness = (gBrightness < 100) ? gBrightness + 10 : 100;
      blSet(gBrightness);
      Screens::setBrightness(gBrightness);
      break;
    case Screens::BUZZER_ON:
    case Screens::BUZZER_OFF: {
      const bool on = (s == Screens::BUZZER_ON);
      BoardIo::buzzerEnable(on);
      Screens::setBuzzer(on);
      break;
    }
    case Screens::NTP_SYNC:
      BoardIo::ntpBegin();            // không chặn; ntpPoll() báo khi xong
      Screens::toast("ĐANG LẤY GIỜ...");
      break;
    case Screens::REBOOT:
      Screens::toast("KHỞI ĐỘNG LẠI...");
      lv_timer_handler();             // kịp vẽ dòng thông báo trước khi tắt
      delay(600);
      ESP.restart();
      break;
  }
}

// --- Tác vụ giao diện --------------------------------------------------------
void uiTaskFn(void *) {
  uint32_t lastTick = millis(), lastDraw = 0, lastSensor = 0, lastClock = 0, lastMem = 0;
  Model local;

  for (;;) {
    const uint32_t now = millis();

    lv_tick_inc(now - lastTick);
    lastTick = now;
    lv_timer_handler();

    BoardIo::buzzerTick();
    blTick();                 // bò dần tới độ sáng đích — xem khối "Tự hạ sáng"
    Screens::tickToast(now);
    if (BoardIo::ntpPoll()) Screens::toast("ĐÃ ĐỒNG BỘ GIỜ");

    // Kết quả từ loop() -> toast.
    Reply r;
    while (replyQ && xQueueReceive(replyQ, &r, 0) == pdTRUE) {
      Screens::toast(r.msg[0] ? r.msg : "ĐÃ GỬI");
    }

    // Đổ số liệu mới. xSemaphoreTake(...,0): KHÔNG chờ — lấy không được thì bỏ
    // lượt này, 200 ms sau lại có ảnh chụp mới. Giao diện đứng hình vì chờ khoá
    // là đúng cái mà kiến trúc hai lõi sinh ra để tránh.
    if (now - lastDraw >= kDrawMs) {
      lastDraw = now;
      if (modelMx && xSemaphoreTake(modelMx, 0) == pdTRUE) {
        memcpy(&local, &shared, sizeof(Model));
        xSemaphoreGive(modelMx);
      }
      Screens::update(local);
    }

    if (now - lastSensor >= kSensorMs) {
      lastSensor = now;
      float t, h;
      if (BoardIo::sht3xRead(t, h)) {
        portENTER_CRITICAL(&indoorMux);
        gIndoorT = t;
        gIndoorH = h;
        portEXIT_CRITICAL(&indoorMux);
      }
    }

    if (now - lastClock >= 1000) {
      lastClock = now;
      BoardIo::Clock c;
      if (BoardIo::clockRead(c)) Screens::setClock(true, c.hh, c.mm);
      else                       Screens::setClock(false, 0, 0);
    }

    // Theo dõi heap LVGL. Giữ lại VĨNH VIỄN chứ không phải đoạn debug tạm: hết
    // heap LVGL không sập máy, nó chỉ bỏ vẽ vài thứ — nghĩa là lỗi này KHÔNG tự
    // báo, người dùng chỉ thấy màn hình thiếu nội dung và tưởng hỏng phần cứng.
    // Đúng ca đã xảy ra khi LV_MEM_SIZE còn 28 KB.
    //
    // LV_MEM_CUSTOM=1 nên lv_mem_monitor() vô dụng (nó chỉ đếm bộ cấp phát riêng
    // của LVGL, mà ta đã bỏ). Đo bằng heap ESP-IDF.
    //
    // "thap nhat" (getMinFreeHeap) mới là con số đáng nhìn, không phải mức tự
    // do hiện tại: nó là ĐÁY từng chạm tới kể từ lúc khởi động, tức là bắt được
    // cả những đợt ngốn bộ nhớ chớp nhoáng — bắt tay WiFi, gói MQTT kèm ir_raw —
    // mà một lần đọc 15 giây một lần chắc chắn bỏ sót. Đáy tụt dần theo giờ là
    // dấu hiệu rò bộ nhớ; đáy dưới ~20 KB là sắp có chuyện.
    if (now - lastMem >= kMemLogMs) {
      lastMem = now;
      // uxTaskGetStackHighWaterMark(nullptr) = chỗ trống ÍT NHẤT mà stack tác vụ
      // này từng còn, tính từ lúc khởi động. Đây là con số quyết định kUiStack —
      // xuống dưới ~1500 B là sắp tràn, mà tràn stack thì bo reset chứ không hề
      // báo lỗi gì.
      Serial.printf("Heap: tu do %u B, thap nhat %u B, khoi lon nhat %u B | "
                    "stack UI con trong it nhat %u B\n",
                    (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMinFreeHeap(),
                    (unsigned)ESP.getMaxAllocHeap(),
                    (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }

    vTaskDelay(pdMS_TO_TICKS(5));   // ~200 Hz: chạm phản hồi tức thì
  }
}

} // namespace

// ===========================================================================
//  API — xem ui.h
// ===========================================================================
bool begin() {
  // OE của TXS0104 (bộ dịch mức ra cổng P3). PHẢI đặt Ở ĐÂY, trong setup(), chứ
  // KHÔNG kéo bằng trở ngoài: GPIO12 = MTDI, nếu nó HIGH lúc reset thì ROM chọn
  // mức flash 1.8V và bo không boot. Trên bo này R7 10k kéo GPIO12 xuống GND —
  // đúng chuẩn, nên chỉ cần kéo lên sau khi đã boot xong.
  pinMode(EN_LEVEL_SHIFT_PIN, OUTPUT);
  digitalWrite(EN_LEVEL_SHIFT_PIN, HIGH);

  modelMx = xSemaphoreCreateMutex();
  cmdQ    = xQueueCreate(4, sizeof(Command));
  replyQ  = xQueueCreate(4, sizeof(Reply));

  BoardIo::backlightBegin(LCD_BACKLIGHT_PIN);
  BoardIo::backlightSet(0);          // bật sáng SAU khi đã vẽ xong khung đầu
  BoardIo::buzzerBegin(BUZZER_PIN);

  // Touch::begin() gọi Wire.begin + ghim 100 kHz -> phải chạy TRƯỚC mọi thứ
  // khác trên bus I2C (DS1307 và SHT3x nằm chung bus đó).
  const bool touchOk =
      Touch::begin(I2C_SDA_PIN, I2C_SCL_PIN, TOUCH_RST_PIN, TOUCH_INT_PIN);
  Serial.printf("Cam ung: %s (SDA=%d SCL=%d RST=%d INT=%d)\n",
                touchOk ? "OK" : "KHONG THAY CHIP tren bus I2C",
                I2C_SDA_PIN, I2C_SCL_PIN, TOUCH_RST_PIN, TOUCH_INT_PIN);
  BoardIo::sht3xBegin();

  tft.init();

  // ĐẢO MÀU. TFT_eSPI gọi ST7789_INVON VÔ ĐIỀU KIỆN trong ST7789_Init.h, không
  // có cờ build nào tắt được — phải gọi tay sau init().
  //
  // ĐÂY LÀ CÔNG TẮC ĐỂ THỬ. Sai chiều thì mọi màu bị lật thành âm bản: nền gần
  // đen ra gần trắng, chữ trắng thành đen, xanh nhấn #0055FF thành cam #FFAA00.
  // Bố cục vẫn đúng y bản thiết kế nên rất dễ tưởng mình chọn sai bảng màu chứ
  // không phải phần cứng lật màu. Chạy colorSelfTest() ngay dưới để biết chiều
  // nào đúng thay vì đoán.
  tft.invertDisplay(DISPLAY_INVERT);

  tft.setRotation(TFT_ROTATION);
  // Nền khởi động màu nền của giao diện, KHÔNG phải trắng: bảng màu là nền tối,
  // chớp một khung trắng rồi tối sầm lại nhìn như bo bị lỗi.
  tft.fillScreen(tft.color565(0x0C, 0x19, 0x3B));

  // Đèn nền phải bật TRƯỚC phép thử màu, nếu không thì không nhìn thấy gì. Đặt
  // thẳng vào phần cứng chứ không qua blSet() vì tác vụ UI chưa chạy.
#if DISPLAY_SELFTEST
  BoardIo::backlightSet(100);
  colorSelfTest();
  gBlNow = 100;   // đồng bộ lại để blTick() không kéo ngược xuống rồi lên
#endif
  tft.setSwapBytes(false);   // cặp đôi với LV_COLOR_16_SWAP=1 — xem flushCb()
  gDma = tft.initDMA();
  Serial.printf("Man hinh: DMA %s\n",
                gDma ? "BAT" : "TAT (chay pushColors thuong — van dung, chi cham hon)");

  // Cấp bộ đệm vẽ từ RAM TRONG có DMA. Hỏng thì hạ dần số dòng chứ không bỏ
  // cuộc: màn hình ít dòng đệm vẫn chạy, chỉ kém mượt — còn không có đệm thì
  // LVGL không vẽ được gì cả, và người dùng chỉ thấy một màn tối câm.
  uint32_t lines = kBufLines;
  while (lines >= 8) {
    const size_t bytes = Theme::SCREEN_W * lines * sizeof(lv_color_t);
    buf1 = (lv_color_t *)heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *)heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (buf1 && buf2) break;
    heap_caps_free(buf1); heap_caps_free(buf2);
    buf1 = buf2 = nullptr;
    lines /= 2;
    Serial.printf("Khong du RAM DMA cho bo dem ve — thu lai voi %u dong\n", lines);
  }
  if (!buf1 || !buf2) {
    Serial.println("KHONG cap phat noi bo dem ve — man hinh se khong hoat dong");
    return false;
  }

  Serial.printf("Bo dem ve: 2 x %u dong (%u B moi bo), den nen GPIO%d\n",
                lines, (unsigned)(Theme::SCREEN_W * lines * sizeof(lv_color_t)),
                LCD_BACKLIGHT_PIN);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, Theme::SCREEN_W * lines);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res  = Theme::SCREEN_W;
  dispDrv.ver_res  = Theme::SCREEN_H;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type    = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchCb;
  lv_indev_drv_register(&indevDrv);

  Theme::init();
  // Bọc qua lambda vì BoardIo::beep có tham số mặc định — kiểu thật của nó là
  // void(uint16_t,uint16_t), không gán thẳng vào void(*)() được.
  Theme::setPressSound([] { BoardIo::beep(); });   // mọi nút kêu bíp, kể cả tab
  Screens::build(onCommand, onSetting);
  Screens::setBrightness(gBrightness);
  Screens::setBuzzer(BoardIo::buzzerEnabled());

  // Vẽ khung đầu RỒI mới bật đèn nền — tránh chớp một màn rác lúc khởi động.
  lv_timer_handler();
  blSet(gBrightness);   // tác vụ UI sẽ kéo lên dần -> sáng dần thay vì loé

  if (!touchOk) {
    // Màn vẫn vẽ bình thường, chỉ là không bấm được. Nói ra để người đi lắp
    // biết ngay tại chỗ thay vì tưởng cả bo hỏng.
    Screens::toast("KHÔNG THẤY CHIP CẢM ỨNG", true);
  }

  xTaskCreatePinnedToCore(uiTaskFn, "ui", kUiStack, nullptr, kUiPrio, &uiTask, kUiCore);
  return touchOk;
}

void publish(const Model &m) {
  if (!modelMx) return;
  // Chờ 0 tick: tác vụ UI đang giữ khoá thì bỏ qua vòng này. loop() KHÔNG được
  // đứng chờ giao diện — đúng lý do đã tách tác vụ ngay từ đầu.
  if (xSemaphoreTake(modelMx, 0) != pdTRUE) return;
  memcpy(&shared, &m, sizeof(Model));
  xSemaphoreGive(modelMx);
}

bool pollCommand(Command &out) {
  return cmdQ && xQueueReceive(cmdQ, &out, 0) == pdTRUE;
}

void reply(const char *msg) {
  if (!replyQ) return;
  Reply r{};
  snprintf(r.msg, sizeof r.msg, "%s", msg ? msg : "ĐÃ GỬI");
  xQueueSend(replyQ, &r, 0);
}

bool readIndoor(float &tempC, float &humidity) {
  portENTER_CRITICAL(&indoorMux);
  const float t = gIndoorT, h = gIndoorH;
  portEXIT_CRITICAL(&indoorMux);
  if (isnan(t) || isnan(h)) return false;   // KHÔNG bao giờ trả 0.0 thay cho "không đo được"
  tempC    = t;
  humidity = h;
  return true;
}

void setIndoor(float tempC, float humidity) {
  portENTER_CRITICAL(&indoorMux);
  gIndoorT = tempC;
  gIndoorH = humidity;
  portEXIT_CRITICAL(&indoorMux);
}

} // namespace Ui
