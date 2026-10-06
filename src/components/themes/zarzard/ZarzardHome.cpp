#include "ZarzardHome.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "ZarzardFlashcards.h"
#include "ZarzardUi.h"
#include "fontIds.h"

namespace ZarzardUi {

namespace {
constexpr int BOOK_CARD_H = 84;
constexpr int TILE_RADIUS = 18;
constexpr int TILE_PAD = 18;
constexpr int ICON_GAP = 14;
constexpr int GAP = 12;
constexpr int TILE_MAX_H = 120;
constexpr int HINTS_RESERVE = 52;  // button hints and the space above them

void drawStudyCard(const GfxRenderer& renderer, const ZarzardHomeView& v, const int x, const int y, const int w) {
  drawCard(renderer, x, y, w, 204, v.studySelected, 22);
  drawIcon(renderer, ZarzardIcons::CardsFill32, x + 22, y + 22);
  // 戦え centered in the card's empty right side, above the progress bar.
  const auto& mark = ZarzardIcons::TatakaeCard;
  drawPicture(renderer, mark, x + 354 - mark.width / 2, y + 88 - mark.height / 2);
  renderer.drawText(ZZ_TITLE_14_FONT_ID, x + 64, y + 18, v.studyTitle);
  if (v.cardsToday < 0) {
    renderer.drawText(UI_12_FONT_ID, x + 22, y + 90, v.dateUnknownLabel, true, EpdFontFamily::BOLD);
    return;
  }
  char num[8];
  snprintf(num, sizeof(num), "%d", v.cardsToday);
  renderer.drawText(ZZ_DIGITS_40_FONT_ID, x + 20, y + 58, num);
  const int textX = x + 32 + renderer.getTextWidth(ZZ_DIGITS_40_FONT_ID, num);
  if (v.cardsToday == 0) {
    renderer.drawText(UI_12_FONT_ID, textX, y + 96, v.allDoneLabel);
  } else {
    renderer.drawText(UI_12_FONT_ID, textX, y + 82, v.cardsLabel);
    renderer.drawText(UI_12_FONT_ID, textX, y + 110, v.dueLabel);
  }
  drawIcon(renderer, ZarzardIcons::FireFill24, x + 22, y + 162);
  renderer.drawText(UI_12_FONT_ID, x + 52, y + 158, v.streakText.c_str(), true, EpdFontFamily::BOLD);
  drawBar(renderer, x + 190, y + 170, w - 212, v.donePercent, true, 10);
}

// Same shape as the tiles: big icon on the left, the title and one small line.
void drawBookCard(const GfxRenderer& renderer, const ZarzardHomeView& v, const int x, const int y, const int w) {
  drawCard(renderer, x, y, w, BOOK_CARD_H, v.bookSelected, TILE_RADIUS);
  const auto& icon = ZarzardIcons::BookOpen48;
  drawIcon(renderer, icon, x + TILE_PAD, y + (BOOK_CARD_H - icon.size) / 2);
  const int textX = x + TILE_PAD + icon.size + ICON_GAP, textW = w - (textX - x) - TILE_PAD;
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  const int textY = y + (BOOK_CARD_H - titleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, v.bookTitle.c_str(), textW, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX, textY, title.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, textX, textY + titleH, v.continueLabel);
}

void drawTile(const GfxRenderer& renderer, const ZarzardHomeTile& t, const int x, const int y, const int w,
              const int h) {
  drawCard(renderer, x, y, w, h, t.selected, TILE_RADIUS);
  drawIcon(renderer, *t.icon, x + TILE_PAD, y + (h - t.icon->size) / 2);
  const int textX = x + TILE_PAD + t.icon->size + ICON_GAP;
  const auto style = t.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const std::string label = renderer.truncatedText(UI_12_FONT_ID, t.label, x + w - TILE_PAD / 2 - textX, style);
  renderer.drawText(UI_12_FONT_ID, textX, y + (h - renderer.getLineHeight(UI_12_FONT_ID)) / 2, label.c_str(), true,
                    style);
}
}  // namespace

void drawHome(const GfxRenderer& renderer, const ZarzardHomeView& view) {
  const int pageWidth = renderer.getScreenWidth();
  const int cardWidth = pageWidth - 2 * GUTTER;

  renderer.drawText(ZZ_TITLE_14_FONT_ID, GUTTER, 16, "zarzard");
  drawBattery(renderer, pageWidth - GUTTER, 26, view.batteryPercent, view.showBatteryPercent);

  int y = 66;
  drawStudyCard(renderer, view, GUTTER, y, cardWidth);
  y += 220;
  if (view.hasBook) {
    drawBookCard(renderer, view, GUTTER, y, cardWidth);
    y += BOOK_CARD_H + GAP;
  }
  // Two columns of wide tiles filling the space down to the button hints.
  const int count = static_cast<int>(view.tiles.size());
  if (count == 0) return;
  const int rows = (count + 1) / 2;
  const int tileW = (cardWidth - GAP) / 2;
  const int bottom = renderer.getScreenHeight() - HINTS_RESERVE;
  const int tileH = std::min(TILE_MAX_H, (bottom - y - (rows - 1) * GAP) / rows);
  for (int i = 0; i < count; ++i) {
    drawTile(renderer, view.tiles[i], GUTTER + (i % 2) * (tileW + GAP), y + (i / 2) * (tileH + GAP), tileW, tileH);
  }
}

}  // namespace ZarzardUi
