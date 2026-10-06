#pragma once
#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;

// Chapter text layout for the Bible screen. Needs only text measuring, so the
// host tests run it with the real fonts.
namespace BibleLayout {

struct Geometry {
  int left = 0;        // x of the verse numbers and of continuation lines
  int width = 0;       // text width
  int textFont = 0;    // verse text (regular)
  int numberFont = 0;  // verse number (bold)
  int numberGap = 0;   // space after the number
};

// One screen line: a slice of the chapter text (no '\n').
struct Line {
  uint32_t start = 0;
  uint16_t length = 0;
  int16_t x = 0;
  uint16_t verse = 0;      // verse number on the first line of a verse, else 0
  bool endsVerse = false;  // extra gap after it
};

// Wraps every verse ("text\n" each) at spaces; a verse continues on the next
// line after its number.
std::vector<Line> layout(const GfxRenderer& r, const std::string& text, const Geometry& g);

// First line of each page. A verse may continue on the next page.
std::vector<uint16_t> paginate(const std::vector<Line>& lines, int lineHeight, int verseGap, int height);

// Page showing line `lineInVerse` of `verse` (0 when not found).
int pageOfVerse(const std::vector<Line>& lines, const std::vector<uint16_t>& pages, int verse, int lineInVerse = 0);

// First line of `verse`, -1 when not found.
int firstLineOfVerse(const std::vector<Line>& lines, int verse);

// Verse shown at the top of a page (the one its first line belongs to).
int verseAtLine(const std::vector<Line>& lines, int line);

}  // namespace BibleLayout
