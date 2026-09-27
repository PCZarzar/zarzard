#pragma once
#include <GfxRenderer.h>

#include <cstdint>
#include <string>

struct Rect;

// Drawing pieces of the statistics screens: bar charts, stacked bars, number
// tiles. Black and dithered gray only, which stay crisp on e-ink.
namespace FlashcardChart {

struct Bar {
  uint32_t base = 0;            // black part (bottom)
  uint32_t top = 0;             // gray part stacked on it
  const char* label = nullptr;  // under the bar; nullptr = none
  bool mark = false;            // highlighted (e.g. today): label in bold, marker under it
};

// Vertical bars scaled to the largest total, with the maximum written at the
// top left and each total above its bar when there is room.
void drawBars(const GfxRenderer& renderer, const Rect& area, const Bar* bars, int count, bool valuesOnTop);

struct Segment {
  uint32_t value = 0;
  Color color = Color::Black;  // White segments get an outline
};

// Horizontal bar split in proportion to the segments, with a frame.
void drawStacked(const GfxRenderer& renderer, int x, int y, int w, int h, const Segment* segments, int count);

// Framed tile: a big value centred over a small label and an optional second line.
void drawTile(const GfxRenderer& renderer, const Rect& rect, const char* value, const char* label,
              const char* sublabel = nullptr);

// Height of a tile with or without the second label line.
int tileHeight(const GfxRenderer& renderer, bool sublabel);

// Filled triangle pointing left or right, centred on (cx, cy).
void drawArrow(const GfxRenderer& renderer, int cx, int cy, int size, bool left);

// Small legend square followed by its text; returns the x after it.
int drawLegend(const GfxRenderer& renderer, int x, int y, Color color, const char* text);

// "0 min", "25 min", "2h05".
std::string formatDuration(uint32_t seconds);

}  // namespace FlashcardChart
