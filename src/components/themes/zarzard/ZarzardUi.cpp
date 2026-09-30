#include "ZarzardUi.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "fontIds.h"

namespace ZarzardUi {

void drawIcon(const GfxRenderer& renderer, const ZarzardIcons::Icon& icon, const int x, const int y, const bool black) {
  const int rowBytes = (icon.size + 7) / 8;
  for (int row = 0; row < icon.size; ++row) {
    for (int col = 0; col < icon.size; ++col) {
      if (icon.bits[row * rowBytes + (col >> 3)] & (0x80 >> (col & 7))) {
        renderer.drawPixel(x + col, y + row, black);
      }
    }
  }
}

void drawPicture(const GfxRenderer& renderer, const ZarzardIcons::Picture& picture, const int x, const int y,
                 const bool black) {
  const int rowBytes = (picture.width + 7) / 8;
  for (int row = 0; row < picture.height; ++row) {
    for (int col = 0; col < picture.width; ++col) {
      if (picture.bits[row * rowBytes + (col >> 3)] & (0x80 >> (col & 7))) {
        renderer.drawPixel(x + col, y + row, black);
      }
    }
  }
}

void drawMark(const GfxRenderer& renderer, const bool black) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const int top = h / 2 - 210;
  const auto& mark = ZarzardIcons::Tatakae;
  drawPicture(renderer, mark, (w - mark.width) / 2, top, black);
  const int ruleTop = top + mark.height + 28;
  renderer.drawLine(w / 2, ruleTop, w / 2, ruleTop + 50, black);
  // Letter-spaced name.
  constexpr const char* NAME = "zarzard";
  constexpr int TRACK = 12;
  int nameW = -TRACK;
  char ch[2] = {0, 0};
  for (const char* p = NAME; *p; ++p) {
    ch[0] = *p;
    nameW += renderer.getTextWidth(ZZ_TITLE_18_FONT_ID, ch) + TRACK;
  }
  int x = (w - nameW) / 2;
  const int nameY = ruleTop + 62;
  for (const char* p = NAME; *p; ++p) {
    ch[0] = *p;
    renderer.drawText(ZZ_TITLE_18_FONT_ID, x, nameY, ch, black);
    x += renderer.getTextWidth(ZZ_TITLE_18_FONT_ID, ch) + TRACK;
  }
  const int rule2 = nameY + renderer.getLineHeight(ZZ_TITLE_18_FONT_ID) + 16;
  renderer.drawLine(w / 2, rule2, w / 2, rule2 + 50, black);
}

void drawCard(const GfxRenderer& renderer, const int x, const int y, const int w, const int h, const bool selected,
              const int radius) {
  if (selected) {
    renderer.fillRoundedRect(x, y, w, h, radius, Color::Black);
  } else {
    renderer.drawRoundedRect(x, y, w, h, 2, radius, true);
  }
}

void drawBar(const GfxRenderer& renderer, const int x, const int y, const int w, const int percent, const bool black,
             const int h) {
  renderer.drawRoundedRect(x, y, w, h, 2, h / 2, black);
  const int fill = (w - 4) * std::clamp(percent, 0, 100) / 100;
  if (fill > 0) {
    renderer.fillRoundedRect(x + 2, y + 2, fill, h - 4, (h - 4) / 2, black ? Color::Black : Color::White);
  }
}

}  // namespace ZarzardUi
