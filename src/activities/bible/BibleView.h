#pragma once
#include <string>
#include <vector>

#include "BibleLayout.h"

class GfxRenderer;
struct Rect;

// Drawing of the Bible screens' content (header and button hints are drawn by
// the activity through the theme). Renderer-only, so the host simulator shows
// exactly what the device draws.
namespace BibleView {

constexpr int GRID_COLUMNS = 6;

// Chapter cells that fit in area (whole rows of GRID_COLUMNS).
int gridCellsPerPage(const GfxRenderer& r, const Rect& area);

// Numbered cells 1..chapters, the page holding `selected` (0-based); the
// selected cell gets a heavy outline. pageLabel (may be empty) goes below.
void drawChapterGrid(const GfxRenderer& r, const Rect& area, int chapters, int selected, const char* pageLabel);

// Lines [first, last) of a chapter from top: verse number in bold, then text.
void drawPage(const GfxRenderer& r, const std::string& text, const std::vector<BibleLayout::Line>& lines, int first,
              int last, int top, const BibleLayout::Geometry& g, int verseGap);

struct FontChoice {
  int fontId;
  const char* label;  // "Normal · 14"
};

// Reading text sizes, each row showing `sample` in its own font.
void drawFontChoices(const GfxRenderer& r, const Rect& area, const FontChoice* choices, int count, int selected,
                     const char* sample);

// One centred line in small text (footers and tips).
void drawNote(const GfxRenderer& r, int y, const char* text);

}  // namespace BibleView
