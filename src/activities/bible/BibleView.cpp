#include "BibleView.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"

namespace BibleView {

namespace {
constexpr int CELL_MAX_WIDTH = 70;
constexpr int CELL_HEIGHT = 56;
constexpr int CELL_INSET = 4;
constexpr int CELL_RADIUS = 10;
constexpr int LABEL_GAP = 8;

int labelHeight(const GfxRenderer& r) { return r.getLineHeight(UI_10_FONT_ID) + LABEL_GAP; }
}  // namespace

int gridCellsPerPage(const GfxRenderer& r, const Rect& area) {
  const int rows = std::max(1, (area.height - labelHeight(r)) / CELL_HEIGHT);
  return rows * GRID_COLUMNS;
}

void drawChapterGrid(const GfxRenderer& r, const Rect& area, const int chapters, const int selected,
                     const char* pageLabel) {
  const int perPage = gridCellsPerPage(r, area);
  const int cellW = std::min(CELL_MAX_WIDTH, area.width / GRID_COLUMNS);
  const int left = area.x + (area.width - cellW * GRID_COLUMNS) / 2;
  const int first = selected / perPage * perPage;
  const int last = std::min(chapters, first + perPage);
  const int textH = r.getLineHeight(UI_12_FONT_ID);
  int bottom = area.y;
  for (int n = first; n < last; ++n) {
    const int i = n - first;
    const int x = left + (i % GRID_COLUMNS) * cellW;
    const int y = area.y + (i / GRID_COLUMNS) * CELL_HEIGHT;
    const bool sel = n == selected;
    // Heavy outline instead of a black block: less ghosting on partial refresh.
    r.drawRoundedRect(x + CELL_INSET, y + CELL_INSET, cellW - 2 * CELL_INSET, CELL_HEIGHT - 2 * CELL_INSET, sel ? 3 : 1,
                      CELL_RADIUS, true);
    char num[8];
    snprintf(num, sizeof(num), "%d", n + 1);
    const auto style = sel ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    r.drawText(UI_12_FONT_ID, x + (cellW - r.getTextWidth(UI_12_FONT_ID, num, style)) / 2,
               y + (CELL_HEIGHT - textH) / 2, num, true, style);
    bottom = y + CELL_HEIGHT;
  }
  if (pageLabel && *pageLabel) {
    const int w = r.getTextWidth(UI_10_FONT_ID, pageLabel);
    r.drawText(UI_10_FONT_ID, area.x + (area.width - w) / 2, bottom + LABEL_GAP, pageLabel);
  }
}

void drawPage(const GfxRenderer& r, const std::string& text, const std::vector<BibleLayout::Line>& lines,
              const int first, const int last, const int top, const BibleLayout::Geometry& g, const int verseGap) {
  const int lineH = r.getLineHeight(g.textFont);
  // Bold small number sits on the text's x-height rather than its top.
  const int numberDrop = (lineH - r.getLineHeight(g.numberFont)) / 2 - 2;
  std::string buf;
  buf.reserve(160);
  int y = top;
  for (int i = first; i < last && i < static_cast<int>(lines.size()); ++i) {
    const auto& l = lines[i];
    if (l.verse != 0) {
      char num[8];
      snprintf(num, sizeof(num), "%u", static_cast<unsigned>(l.verse));
      r.drawText(g.numberFont, g.left, y + numberDrop, num, true, EpdFontFamily::BOLD);
    }
    buf.assign(text, l.start, l.length);
    r.drawText(g.textFont, l.x, y, buf.c_str());
    y += lineH + (l.endsVerse ? verseGap : 0);
  }
}

void drawFontChoices(const GfxRenderer& r, const Rect& area, const FontChoice* choices, const int count,
                     const int selected, const char* sample) {
  constexpr int ROW_PAD = 14;
  constexpr int RADIUS = 12;
  int y = area.y;
  for (int i = 0; i < count; ++i) {
    const int lineH = r.getLineHeight(choices[i].fontId);
    const int labelH = r.getLineHeight(SMALL_FONT_ID);
    const int rowH = ROW_PAD + labelH + lineH + ROW_PAD;
    if (i == selected) {
      r.drawRoundedRect(area.x, y, area.width, rowH, 3, RADIUS, true);
    }
    r.drawText(SMALL_FONT_ID, area.x + ROW_PAD, y + ROW_PAD, choices[i].label);
    // The sample is cut to the row width so every size shows the same words.
    std::string text = sample;
    while (!text.empty() && r.getTextWidth(choices[i].fontId, text.c_str()) > area.width - 2 * ROW_PAD) {
      const size_t cut = text.rfind(' ');
      text.resize(cut == std::string::npos ? 0 : cut);
    }
    r.drawText(choices[i].fontId, area.x + ROW_PAD, y + ROW_PAD + labelH, text.c_str());
    y += rowH + 8;
  }
}

void drawNote(const GfxRenderer& r, const int y, const char* text) {
  const int w = r.getTextWidth(SMALL_FONT_ID, text);
  r.drawText(SMALL_FONT_ID, (r.getScreenWidth() - w) / 2, y, text);
}

}  // namespace BibleView
