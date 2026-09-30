#include "ZarzardFlashcards.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "ZarzardUi.h"
#include "fontIds.h"

namespace ZarzardUi {

void frontButtonCenters(const GfxRenderer& renderer, int out[4]) {
  constexpr int narrow[] = {98, 186, 294, 382};
  constexpr int wide[] = {105, 197, 331, 423};
  const int* src = renderer.getScreenWidth() >= 528 ? wide : narrow;
  std::copy(src, src + 4, out);
}

void drawActionButtons(const GfxRenderer& renderer, const ActionButton buttons[4]) {
  int centers[4];
  frontButtonCenters(renderer, centers);
  constexpr int W = 84;
  const int top = renderer.getScreenHeight() - ACTION_BUTTON_HEIGHT - 8;
  for (int i = 0; i < 4; ++i) {
    const ActionButton& b = buttons[i];
    if (!b.icon) continue;
    const int x = centers[i] - W / 2;
    renderer.fillRoundedRect(x, top, W, ACTION_BUTTON_HEIGHT, 18, Color::White);
    renderer.drawRoundedRect(x, top, W, ACTION_BUTTON_HEIGHT, 2, 18, true);
    const bool twoLines = b.sub && b.sub[0];
    drawIcon(renderer, *b.icon, centers[i] - b.icon->size / 2, top + (twoLines ? 8 : 14));
    const int labelY = top + (twoLines ? 42 : 52);
    renderer.drawText(SMALL_FONT_ID, centers[i] - renderer.getTextWidth(SMALL_FONT_ID, b.label) / 2, labelY, b.label);
    if (twoLines) {
      renderer.drawText(SMALL_FONT_ID, centers[i] - renderer.getTextWidth(SMALL_FONT_ID, b.sub) / 2, labelY + 20,
                        b.sub);
    }
  }
}

int drawCardHeader(const GfxRenderer& renderer, const char* title, const char* right, const int percent) {
  const int w = renderer.getScreenWidth();
  const int rightW = right && right[0] ? renderer.getTextWidth(UI_12_FONT_ID, right, EpdFontFamily::BOLD) : 0;
  if (rightW) renderer.drawText(UI_12_FONT_ID, w - GUTTER - rightW, 14, right, true, EpdFontFamily::BOLD);
  const std::string t =
      renderer.truncatedText(UI_12_FONT_ID, title, w - 2 * GUTTER - (rightW ? rightW + 16 : 0), EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, GUTTER, 14, t.c_str(), true, EpdFontFamily::BOLD);
  drawBar(renderer, GUTTER, 54, w - 2 * GUTTER, percent, true, 8);
  return 76;
}

void drawCardFrame(const GfxRenderer& renderer, const int top, const int bottom) {
  const int w = renderer.getScreenWidth();
  renderer.drawRoundedRect(GUTTER, top, w - 2 * GUTTER, bottom - top, 2, 22, true);
}

void drawBattery(const GfxRenderer& renderer, const int right, const int y, const int percent, const bool showText) {
  constexpr int W = 26, H = 13;
  const int x = right - W - 3;
  renderer.drawRoundedRect(x, y, W, H, 2, 3, true);
  renderer.fillRect(x + W, y + 4, 3, 5, true);
  renderer.fillRect(x + 3, y + 3, (W - 6) * std::clamp(percent, 0, 100) / 100, H - 6, true);
  if (showText) {
    char text[8];
    snprintf(text, sizeof(text), "%d%%", percent);
    renderer.drawText(UI_10_FONT_ID, x - 6 - renderer.getTextWidth(UI_10_FONT_ID, text), y - 7, text);
  }
}

void drawFlashcardHub(const GfxRenderer& renderer, const FlashcardHubView& v) {
  const int pageW = renderer.getScreenWidth();
  const int cardW = pageW - 2 * GUTTER;
  const int halfW = (cardW - 16) / 2;
  renderer.drawText(ZZ_TITLE_14_FONT_ID, GUTTER, 16, v.title);
  drawBattery(renderer, pageW - GUTTER, 26, v.batteryPercent, v.showBatteryPercent);

  int y = 66;
  // Spaced: today's cards.
  {
    const bool sel = v.selected == 0, ink = !sel;
    drawCard(renderer, GUTTER, y, halfW, 290, sel, 22);
    drawIcon(renderer, sel ? ZarzardIcons::CalendarCheckFill40 : ZarzardIcons::CalendarCheck40, GUTTER + 20, y + 20,
             ink);
    if (v.cardsToday >= 0) {
      char num[8];
      snprintf(num, sizeof(num), "%d", v.cardsToday);
      renderer.drawText(ZZ_DIGITS_40_FONT_ID, GUTTER + 18, y + 84, num, ink);
      renderer.drawText(UI_10_FONT_ID, GUTTER + 20, y + 244, v.dueLabel, ink);
    } else {
      const auto lines = renderer.wrappedText(UI_10_FONT_ID, v.dateUnknownLabel, halfW - 36, 3);
      int ly = y + 104;
      for (const auto& l : lines) {
        renderer.drawText(UI_10_FONT_ID, GUTTER + 20, ly, l.c_str(), ink);
        ly += renderer.getLineHeight(UI_10_FONT_ID);
      }
    }
    renderer.drawText(ZZ_TITLE_14_FONT_ID, GUTTER + 20, y + 204, v.spacedLabel, ink);
  }
  // Linear.
  {
    const int x = GUTTER + halfW + 16;
    const bool sel = v.selected == 1, ink = !sel;
    drawCard(renderer, x, y, halfW, 290, sel, 22);
    drawIcon(renderer, ZarzardIcons::ListNumbers40, x + 20, y + 20, ink);
    renderer.drawText(ZZ_TITLE_14_FONT_ID, x + 20, y + 204, v.linearLabel, ink);
  }
  y += 306;
  // Statistics: this week's cards per day.
  {
    const bool sel = v.selected == 2, ink = !sel;
    drawCard(renderer, GUTTER, y, cardW, 180, sel, 22);
    drawIcon(renderer, ZarzardIcons::ChartBar40, GUTTER + 20, y + 18, ink);
    renderer.drawText(ZZ_TITLE_14_FONT_ID, GUTTER + 72, y + 18, v.statsLabel, ink);
    const uint16_t peak = std::max<uint16_t>(1, *std::max_element(std::begin(v.week), std::end(v.week)));
    constexpr int BAR_W = 16, STEP = 28, MAX_H = 64;
    const int base = y + 152;
    for (int i = 0; i < 7; ++i) {
      const int h = v.week[i] == 0 ? 4 : std::max(8, v.week[i] * MAX_H / peak);
      const int bx = GUTTER + 22 + i * STEP;
      if (sel) {
        renderer.fillRoundedRect(bx, base - h, BAR_W, h, 4, Color::White);
      } else {
        renderer.fillRoundedRect(bx, base - h, BAR_W, h, 4, Color::Black);
      }
    }
    char total[12];
    snprintf(total, sizeof(total), "%u", static_cast<unsigned>(v.weekTotal));
    const int right = GUTTER + cardW - 20;
    renderer.drawText(ZZ_TITLE_18_FONT_ID, right - renderer.getTextWidth(ZZ_TITLE_18_FONT_ID, total), y + 80, total,
                      ink);
    renderer.drawText(UI_10_FONT_ID, right - renderer.getTextWidth(UI_10_FONT_ID, v.weekLabel), y + 128, v.weekLabel,
                      ink);
  }
  y += 196;
  // Settings.
  {
    const bool sel = v.selected == 3, ink = !sel;
    drawCard(renderer, GUTTER, y, cardW, 76, sel, 20);
    drawIcon(renderer, ZarzardIcons::GearSix32, GUTTER + 20, y + 22, ink);
    renderer.drawText(ZZ_TITLE_14_FONT_ID, GUTTER + 66, y + 18, v.settingsLabel, ink);
  }
}

}  // namespace ZarzardUi
