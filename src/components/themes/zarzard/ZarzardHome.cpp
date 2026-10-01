#include "ZarzardHome.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "ZarzardFlashcards.h"
#include "ZarzardUi.h"
#include "fontIds.h"

namespace ZarzardUi {

namespace {
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

void drawBookCard(const GfxRenderer& renderer, const ZarzardHomeView& v, const int x, const int y, const int w) {
  drawCard(renderer, x, y, w, 104, v.bookSelected);
  drawIcon(renderer, ZarzardIcons::BookOpen32, x + 20, y + 36);
  const int textX = x + 66, textW = w - 86;
  renderer.drawText(UI_10_FONT_ID, textX, y + 12, v.continueLabel);
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, v.bookTitle.c_str(), textW, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX, y + 36, title.c_str(), true, EpdFontFamily::BOLD);
  if (!v.bookAuthor.empty()) {
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, v.bookAuthor.c_str(), textW);
    renderer.drawText(UI_10_FONT_ID, textX, y + 66, author.c_str());
  }
}

void drawTile(const GfxRenderer& renderer, const ZarzardHomeTile& t, const int x, const int y, const int w,
              const int h) {
  drawCard(renderer, x, y, w, h, t.selected, 18);
  drawIcon(renderer, *t.icon, x + (w - t.icon->size) / 2, y + 22);
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, t.label, w - 12, 2);
  int lineY = y + (lines.size() == 1 ? 84 : 74);
  for (const auto& line : lines) {
    renderer.drawText(UI_10_FONT_ID, x + (w - renderer.getTextWidth(UI_10_FONT_ID, line.c_str())) / 2, lineY,
                      line.c_str());
    lineY += renderer.getTextHeight(UI_10_FONT_ID) + 4;
  }
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
    y += 120;
  }
  constexpr int GAP = 12;
  const int tileW = (cardWidth - 2 * GAP) / 3;
  constexpr int TILE_H = 132;
  for (size_t i = 0; i < view.tiles.size(); ++i) {
    const int col = static_cast<int>(i % 3), row = static_cast<int>(i / 3);
    drawTile(renderer, view.tiles[i], GUTTER + col * (tileW + GAP), y + row * (TILE_H + GAP), tileW, TILE_H);
  }
}

}  // namespace ZarzardUi
