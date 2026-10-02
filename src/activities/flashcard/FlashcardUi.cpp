#include "FlashcardUi.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"

namespace FlashcardUi {

void drawRows(GfxRenderer& renderer, const Rect& rect, const Row* rows, const int count, const int selected) {
  if (count <= 0) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sidePad = metrics.contentSidePadding;
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  const int subH = renderer.getLineHeight(SMALL_FONT_ID);

  bool anySubtitle = false;
  for (int i = 0; i < count; ++i) {
    if (rows[i].subtitle && rows[i].subtitle[0]) anySubtitle = true;
  }
  constexpr int rowPad = 10;
  const int rowH = rowPad * 2 + titleH + (anySubtitle ? subH + 2 : 0);
  const int visible = std::max(1, rect.height / rowH);
  const int first = std::clamp(selected - visible + 1, 0, std::max(0, count - visible));

  int y = rect.y;
  for (int i = first; i < count && i < first + visible; ++i) {
    const Row& row = rows[i];
    const int x = rect.x + sidePad;
    const int w = rect.width - sidePad * 2;
    if (row.header) {
      // Section label, bottom-aligned in its slot with a rule under it.
      const int textY = y + rowH - rowPad - titleH;
      renderer.drawText(UI_12_FONT_ID, x + 14, textY, row.title, true, EpdFontFamily::BOLD);
      renderer.drawLine(x + 14, y + rowH - 4, x + w - 14, y + rowH - 4, true);
      y += rowH;
      continue;
    }
    // Selected: a heavy outline, not a black fill (black blocks ghost on e-ink).
    const bool sel = (i == selected);
    if (sel) renderer.drawRoundedRect(x, y + 2, w, rowH - 4, 4, 12, true);

    int valueW = 0;
    if (row.value && row.value[0]) {
      valueW = renderer.getTextWidth(UI_12_FONT_ID, row.value, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, x + w - 14 - valueW, y + rowPad, row.value, true, EpdFontFamily::BOLD);
    }
    // Multiple choice: a box before the title, ticked when chosen.
    int textX = x + 14;
    if (row.check >= 0) {
      constexpr int BOX = 22;
      const int boxY = y + rowPad + (titleH - BOX) / 2;
      if (row.check > 0) {
        // Ticked: a filled box with a white tick (small, so it does not ghost).
        renderer.fillRoundedRect(textX, boxY, BOX, BOX, 5, Color::Black);
        const int xs[] = {textX + 5, textX + 9, textX + 17};
        const int ys[] = {boxY + 11, boxY + 16, boxY + 6};
        renderer.drawLine(xs[0], ys[0], xs[1], ys[1], 3, false);
        renderer.drawLine(xs[1], ys[1], xs[2], ys[2], 3, false);
      } else {
        renderer.drawRoundedRect(textX, boxY, BOX, BOX, 2, 5, true);
      }
      textX += BOX + 12;
    }
    const int titleMaxW = w - (textX - x) - 14 - (valueW ? valueW + 10 : 0);
    const std::string title =
        renderer.truncatedText(UI_12_FONT_ID, row.title, titleMaxW, sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(UI_12_FONT_ID, textX, y + rowPad, title.c_str(), true,
                      sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (row.subtitle && row.subtitle[0]) {
      const std::string sub = renderer.truncatedText(SMALL_FONT_ID, row.subtitle, w - (textX - x) - 14);
      renderer.drawText(SMALL_FONT_ID, textX, y + rowPad + titleH + 2, sub.c_str());
    }
    y += rowH;
  }
}

}  // namespace FlashcardUi
