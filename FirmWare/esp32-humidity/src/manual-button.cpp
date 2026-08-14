#include "manual-button.h"

#include "settings.h"

namespace ManualButton {
namespace {

uint8_t g_pin = 255;

bool     g_stable = false;      ///< trạng thái đã lọc rung: true = đang bấm
bool     g_lastRaw = false;
uint32_t g_lastChangeMs = 0;

uint32_t g_pressStartMs = 0;
bool     g_longFired = false;   ///< đã phát LONG_PRESS cho lần giữ này chưa

}  // namespace

void begin(uint8_t pin) {
  g_pin = pin;
  // INPUT_PULLUP dù DevKit V1 đã có trở kéo ngoài trên GPIO0: bo nhái không
  // phải cái nào cũng hàn con trở đó, và thiếu nó thì chân thả nổi — nút "tự
  // bấm" theo nhiễu, tức máy xông tự bật lúc nửa đêm.
  pinMode(g_pin, INPUT_PULLUP);
  g_lastChangeMs = millis();
}

Event poll() {
  if (g_pin == 255) return Event::NONE;

  const bool raw = (digitalRead(g_pin) == LOW);  // nút kéo xuống GND khi bấm
  const uint32_t now = millis();

  if (raw != g_lastRaw) {
    g_lastRaw = raw;
    g_lastChangeMs = now;
    return Event::NONE;  // đang nảy, chưa tin
  }

  // Chưa ổn định đủ lâu thì chưa kết luận.
  if (now - g_lastChangeMs < DEBOUNCE_MS) {
    // Vẫn phải xét ngưỡng giữ ở dưới, nhưng chỉ khi đã ở trạng thái bấm ổn định.
    if (!g_stable) return Event::NONE;
  }

  // --- đổi trạng thái ổn định ---
  if (raw != g_stable && now - g_lastChangeMs >= DEBOUNCE_MS) {
    g_stable = raw;
    if (raw) {
      g_pressStartMs = now;
      g_longFired = false;
      return Event::NONE;  // mới bấm xuống, chưa biết ngắn hay dài
    }
    // Vừa nhả ra.
    if (g_longFired) return Event::NONE;  // đã tính là nhấn giữ rồi, nuốt
    return Event::SHORT_PRESS;
  }

  // --- đang giữ: đủ ngưỡng thì phát ngay, không đợi nhả ---
  if (g_stable && !g_longFired && now - g_pressStartMs >= BUTTON_LONG_MS) {
    g_longFired = true;
    return Event::LONG_PRESS;
  }

  return Event::NONE;
}

}  // namespace ManualButton
