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
    const bool inv = (i == selected);
    if (inv) renderer.fillRoundedRect(x, y + 2, w, rowH - 4, 12, Color::Black);

    int valueW = 0;
    if (row.value && row.value[0]) {
      valueW = renderer.getTextWidth(UI_12_FONT_ID, row.value, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, x + w - 14 - valueW, y + rowPad, row.value, !inv, EpdFontFamily::BOLD);
    }
    const int titleMaxW = w - 28 - (valueW ? valueW + 10 : 0);
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, row.title, titleMaxW);
    renderer.drawText(UI_12_FONT_ID, x + 14, y + rowPad, title.c_str(), !inv);
    if (row.subtitle && row.subtitle[0]) {
      const std::string sub = renderer.truncatedText(SMALL_FONT_ID, row.subtitle, w - 28);
      renderer.drawText(SMALL_FONT_ID, x + 14, y + rowPad + titleH + 2, sub.c_str(), !inv);
    }
    y += rowH;
  }
}

}  // namespace FlashcardUi
