#include "ZarzardUi.h"

#include <GfxRenderer.h>

#include <algorithm>

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
  const auto& mark = ZarzardIcons::SleepMark;
  drawPicture(renderer, mark, (renderer.getScreenWidth() - mark.width) / 2,
              renderer.getScreenHeight() / 2 - ZarzardIcons::SLEEP_MARK_ABOVE_CENTER, black);
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
