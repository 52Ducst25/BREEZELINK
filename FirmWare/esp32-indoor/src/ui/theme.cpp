#include "theme.h"

namespace Theme {

uint16_t thermalColor(float c) {
  if (isnan(c)) return whiteDim;      // không có số -> màu "không biết", không phải màu lạnh
  if (c < 22.0f) return thermalCold;
  if (c < 27.0f) return thermalNeutral;
  if (c < 32.0f) return thermalWarm;
  return thermalHot;
}

Rect modeRect(uint8_t i) {
  return {(int16_t)(PAD + MODE_STEP * i), MODE_Y, MODE_W, MODE_H};
}

Rect settingRow(uint8_t i) {
  return {PAD, (int16_t)(SET_Y0 + SET_STEP * i), (int16_t)(SCREEN_W - 2 * PAD), SET_H};
}

void chamferRect(TFT_eSPI &g, const Rect &r, uint16_t fill, uint16_t border, int16_t cut) {
  const int16_t x = r.x, y = r.y, w = r.w, h = r.h;

  if (fill != TFT_TRANSPARENT) {
    // Ba dải ngang: dải trên thụt vào [cut] bên trái, dải dưới thụt vào bên
    // phải — đúng hai góc bị vát. Không cần fillTriangle: phần bị vát là phần
    // KHÔNG tô, nên chỉ việc chừa ra.
    g.fillRect(x + cut, y,             w - cut, cut,         fill);
    g.fillRect(x,       y + cut,       w,       h - 2 * cut, fill);
    g.fillRect(x,       y + h - cut,   w - cut, cut,         fill);
  }

  g.drawLine(x + cut,     y,           x + w - 1,       y,           border);
  g.drawLine(x + w - 1,   y,           x + w - 1,       y + h - 1 - cut, border);
  g.drawLine(x + w - 1 - cut, y + h - 1, x,             y + h - 1,   border);
  g.drawLine(x,           y + h - 1,   x,               y + cut,     border);
  g.drawLine(x,           y + cut,     x + cut,         y,           border);   // vát trên-trái
  g.drawLine(x + w - 1,   y + h - 1 - cut, x + w - 1 - cut, y + h - 1, border); // vát dưới-phải
}

void panel(TFT_eSPI &g, const Rect &r) {
  chamferRect(g, r, carbonPanel, carbonLine);
}

void button(TFT_eSPI &g, const Rect &r, const char *label, bool active, bool enabled) {
  const uint16_t bg     = !enabled ? carbonUp : (active ? ice : carbonPanel);
  const uint16_t bd     = !enabled ? carbonLine : (active ? ice : carbonLineHi);
  const uint16_t fg     = !enabled ? whiteDim : white;

  chamferRect(g, r, bg, bd);
  g.setFreeFont(&FreeSansBold12pt7b);
  g.setTextColor(fg, bg);
  g.setTextDatum(MC_DATUM);
  g.drawString(label, r.x + r.w / 2, r.y + r.h / 2 + 1);
  g.setTextDatum(TL_DATUM);
}

void badge(TFT_eSPI &g, int16_t x, int16_t y, const char *text, uint16_t color) {
  g.setTextFont(1);
  g.setTextSize(1);
  const int16_t w = (int16_t)strlen(text) * 6 + 12;
  const Rect r = {x, y, w, 16};
  chamferRect(g, r, carbonPanel, color, 3);
  g.setTextColor(color, carbonPanel);
  g.setTextDatum(MC_DATUM);
  g.drawString(text, x + w / 2, y + 8);
  g.setTextDatum(TL_DATUM);
}

} // namespace Theme
