#pragma once
#include <string>
#include <vector>

class GfxRenderer;

// Card text layout (spec step 5). Only needs GfxRenderer::getTextWidth and
// getLineHeight, so it can be exercised on the host with the real fonts.
namespace FlashcardLayout {

// Wrap at spaces (and '\n'); words wider than maxWidth are split by UTF-8
// codepoint, in which case *wordCut is set.
std::vector<std::string> wrapText(const GfxRenderer& r, int fontId, const std::string& text, int maxWidth,
                                  bool* wordCut = nullptr);

// 5.2: largest NotoSerif 24..14 that fits availH without splitting a word
// (24-20 only up to 4 lines); otherwise 14 with codepoint splitting.
int pickMainFont(const GfxRenderer& r, const std::string& text, int maxWidth, int availH,
                 std::vector<std::string>& outLines);

// 5.3: back_content entirely wrapped in backticks.
bool isMonoAnswer(const std::string& back);

// 5.3: largest JetBrains Mono 24..14 on one line; else break after '/' or
// ' ' (never inside a token) at the largest of 20..14 that fits.
int pickMonoFont(const GfxRenderer& r, const std::string& text, int maxWidth, int availH,
                 std::vector<std::string>& outLines);

// Card screens (spec 5.4). Geometry is computed here and only drawn by
// FlashcardReviewActivity, so host tests check exactly what is rendered.
struct CardArea {
  int top;      // first usable y below the header
  int bottom;   // last usable y above the button hints
  int width;    // text width (screen minus side padding)
  int spacing;  // theme vertical spacing
};

struct FrontLayout {
  int mainFont = 0;
  std::vector<std::string> mainLines;
  int mainY = 0;
  int hintFont = 0;
  std::vector<std::string> hintLines;
  int hintY = 0;
};

// Main text vertically centred (5.2 font rule), hint right below it.
FrontLayout layoutFront(const GfxRenderer& r, const std::string& main, const std::string& hint, const CardArea& area,
                        int hintFont);

struct BackLayout {
  int questionFont = 0;
  std::vector<std::string> questionLines;
  int questionY = 0;
  int separatorY = 0;
  int answerFont = 0;
  std::vector<std::string> answerLines;
  int answerY = 0;
};

// Question (frontMain) small at the top, separator, answer vertically centred
// in the remaining space (5.3 mono rule or 5.2). Lines that would overflow the
// area are dropped, never drawn over the button hints.
BackLayout layoutBack(const GfxRenderer& r, const std::string& question, const std::string& backContent,
                      const CardArea& area, int questionFont);

}  // namespace FlashcardLayout
