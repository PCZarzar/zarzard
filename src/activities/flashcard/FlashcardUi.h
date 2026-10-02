#pragma once
#include <cstdint>
#include <string>

class GfxRenderer;
struct Rect;

// Small list renderer shared by the flashcard screens (CrossPoint's list
// screens use FreeInkUI, which these simple state screens do not need).
namespace FlashcardUi {

struct Row {
  const char* title = "";
  const char* subtitle = nullptr;  // optional second line, small font
  const char* value = nullptr;     // optional right-aligned value
  bool header = false;             // section label: not selectable, never highlighted
  int8_t check = -1;               // -1 no box, 0 empty box, 1 ticked box (multiple choice)
};

// Draws rows inside rect, scrolling so that `selected` is visible; the
// selected row gets a heavy outline.
void drawRows(GfxRenderer& renderer, const Rect& rect, const Row* rows, int count, int selected);

}  // namespace FlashcardUi
