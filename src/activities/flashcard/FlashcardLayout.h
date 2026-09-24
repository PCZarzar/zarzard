#pragma once
#include <string>
#include <vector>

class GfxRenderer;

// Card text layout (spec step 5). Only needs GfxRenderer::getTextWidth and
// getLineHeight, so it can be exercised on the host with the real fonts.
namespace FlashcardLayout {

// Wrap at spaces (and '\n'); words wider than maxWidth are split by UTF-8
// codepoint, in which case *wordCut is set.
std::vector<std::string> wrapText(GfxRenderer& r, int fontId, const std::string& text, int maxWidth,
                                  bool* wordCut = nullptr);

// 5.2: largest NotoSerif 24..14 that fits availH in <= 4 lines without
// splitting a word; otherwise 14 with codepoint splitting.
int pickMainFont(GfxRenderer& r, const std::string& text, int maxWidth, int availH, std::vector<std::string>& outLines);

// 5.3: back_content entirely wrapped in backticks.
bool isMonoAnswer(const std::string& back);

// 5.3: largest JetBrains Mono 24..14 on one line; else break after '/' or
// ' ' (never inside a token) at the largest of 20..14 that fits.
int pickMonoFont(GfxRenderer& r, const std::string& text, int maxWidth, int availH, std::vector<std::string>& outLines);

}  // namespace FlashcardLayout
