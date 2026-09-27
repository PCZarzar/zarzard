#pragma once
#include <GfxRenderer.h>

#include <cstdint>
#include <string>

struct Rect;

// Drawing pieces of the statistics screens: bar charts, stacked bars, number
// tiles. Pure black and white only: black, sparse dots or empty (no gray).
namespace FlashcardChart {

enum class Fill : uint8_t { Black, Dots, White };

struct Bar {
  uint32_t base = 0;            // black part (bottom)
  uint32_t top = 0;             // dotted part stacked on it
  const char* label = nullptr;  // under the bar; nullptr = none
  bool mark = false;            // highlighted (e.g. today): label in bold, marker under it
};

// Vertical bars scaled to the largest total, with the maximum written at the
// top left and each total above its bar when there is room.
void drawBars(const GfxRenderer& renderer, const Rect& area, const Bar* bars, int count, bool valuesOnTop);

struct Segment {
  uint32_t value = 0;
  Fill fill = Fill::Black;
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
int drawLegend(const GfxRenderer& renderer, int x, int y, Fill fill, const char* text);

// One labelled horizontal black bar per value, scaled to the largest, with the
// value after it. Returns the height used.
int drawHBars(const GfxRenderer& renderer, int x, int y, int w, const char* const* labels, const uint32_t* values,
              int count);

// "0 min", "25 min", "2h05".
std::string formatDuration(uint32_t seconds);

}  // namespace FlashcardChart
