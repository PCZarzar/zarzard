#include "ZarzardTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <cstring>

#include "fontIds.h"

namespace {
constexpr int PILL_WIDTH = 84;
constexpr int PILL_HEIGHT = 30;
constexpr int PILL_BOTTOM_GAP = 8;
constexpr int HINT_TEXT_Y = 3;  // centers the small font's capitals in the pill

void drawArrow(const GfxRenderer& renderer, const int cx, const int cy, const bool up) {
  const int xs[] = {cx - 6, cx + 6, cx};
  const int ys[] = {up ? cy + 4 : cy - 4, up ? cy + 4 : cy - 4, up ? cy - 5 : cy + 5};
  renderer.fillPolygon(xs, ys, 3, true);
}
}  // namespace

void ZarzardTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                   const char* btn4) const {
  if (gpio.hasTouch()) {
    return;
  }
  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  // Centers over the four front buttons (the 528-wide X3 spaces them wider).
  constexpr int narrowCenters[] = {98, 186, 294, 382};
  constexpr int wideCenters[] = {105, 197, 331, 423};
  const int* centers = renderer.getScreenWidth() >= 528 ? wideCenters : narrowCenters;
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const int top = pageHeight - PILL_HEIGHT - PILL_BOTTOM_GAP;

  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;  // unused buttons show nothing
    const int x = centers[i] - PILL_WIDTH / 2;
    renderer.fillRoundedRect(x, top, PILL_WIDTH, PILL_HEIGHT, PILL_HEIGHT / 2, Color::White);
    renderer.drawRoundedRect(x, top, PILL_WIDTH, PILL_HEIGHT, 2, PILL_HEIGHT / 2, true);
    if (strcmp(labels[i], tr(STR_DIR_UP)) == 0 || strcmp(labels[i], tr(STR_DIR_DOWN)) == 0) {
      drawArrow(renderer, centers[i], top + PILL_HEIGHT / 2, strcmp(labels[i], tr(STR_DIR_UP)) == 0);
    } else {
      drawHintLabel(renderer, SMALL_FONT_ID, labels[i], x, PILL_WIDTH, top, PILL_HEIGHT, HINT_TEXT_Y);
    }
  }
  renderer.setOrientation(origOrientation);
}
