#include "BibleLayout.h"

#include <GfxRenderer.h>

#include <cstdio>

namespace BibleLayout {

std::vector<Line> layout(const GfxRenderer& r, const std::string& text, const Geometry& g) {
  std::vector<Line> lines;
  // About three lines per verse; avoids most regrowth for long chapters.
  size_t verses = 0;
  for (const char c : text) verses += c == '\n';
  lines.reserve(verses * 3);

  std::string candidate;
  candidate.reserve(160);
  uint16_t verse = 0;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    ++verse;
    char num[8];
    snprintf(num, sizeof(num), "%u", static_cast<unsigned>(verse));
    const int indent = r.getTextWidth(g.numberFont, num, EpdFontFamily::BOLD) + g.numberGap;
    int x = g.left + indent;
    int avail = g.width - indent;
    bool first = true;
    size_t lineStart = pos;
    size_t lineEnd = pos;  // end of the text accepted on this line
    auto push = [&] {
      Line l;
      l.start = static_cast<uint32_t>(lineStart);
      l.length = static_cast<uint16_t>(lineEnd - lineStart);
      l.x = static_cast<int16_t>(x);
      l.verse = first ? verse : 0;
      lines.push_back(l);
      first = false;
    };
    size_t i = pos;
    while (i < end) {
      size_t wordEnd = text.find(' ', i);
      if (wordEnd == std::string::npos || wordEnd > end) wordEnd = end;
      if (lineEnd > lineStart) {
        candidate.assign(text, lineStart, wordEnd - lineStart);
        if (r.getTextWidth(g.textFont, candidate.c_str()) > avail) {
          push();
          x = g.left;
          avail = g.width;
          lineStart = i;
        }
      }
      lineEnd = wordEnd;
      i = wordEnd + 1;
    }
    push();
    lines.back().endsVerse = true;
    pos = end + 1;
  }
  return lines;
}

std::vector<uint16_t> paginate(const std::vector<Line>& lines, const int lineHeight, const int verseGap,
                               const int height) {
  std::vector<uint16_t> pages;
  pages.reserve(lines.size() / 12 + 1);
  pages.push_back(0);
  int y = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (y + lineHeight > height && y > 0) {
      pages.push_back(static_cast<uint16_t>(i));
      y = 0;
    }
    y += lineHeight + (lines[i].endsVerse ? verseGap : 0);
  }
  return pages;
}

int firstLineOfVerse(const std::vector<Line>& lines, const int verse) {
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].verse == verse) return static_cast<int>(i);
  }
  return -1;
}

int pageOfVerse(const std::vector<Line>& lines, const std::vector<uint16_t>& pages, const int verse,
                const int lineInVerse) {
  int line = firstLineOfVerse(lines, verse);
  if (line < 0) return 0;
  // Stay inside the verse even if the saved offset came from another layout.
  for (int k = 0; k < lineInVerse && line + 1 < static_cast<int>(lines.size()) && lines[line + 1].verse == 0; ++k) {
    ++line;
  }
  int page = 0;
  for (size_t p = 0; p < pages.size(); ++p) {
    if (pages[p] <= line) page = static_cast<int>(p);
  }
  return page;
}

int verseAtLine(const std::vector<Line>& lines, int line) {
  if (lines.empty()) return 1;
  if (line >= static_cast<int>(lines.size())) line = static_cast<int>(lines.size()) - 1;
  while (line > 0 && lines[line].verse == 0) --line;
  return lines[line].verse == 0 ? 1 : lines[line].verse;
}

}  // namespace BibleLayout
