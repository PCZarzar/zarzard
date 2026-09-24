#include "FlashcardLayout.h"

#include <GfxRenderer.h>
#include <Utf8.h>

#include <algorithm>

#include "FlashcardDeck.h"
#include "fontIds.h"

namespace FlashcardLayout {

// ─── Text wrapping helpers ───────────────────────────────────────────────────
// Wrap plain text so each line fits within maxWidth pixels. Breaks at spaces
// when possible; for space-less scripts (CJK) or over-long words falls back to
// UTF-8 codepoint boundaries so multi-byte characters never get split mid-byte.

static bool isUtf8Start(unsigned char b) { return (b & 0xC0) != 0x80; }

static void wrapByCodepoint(const GfxRenderer& r, int fontId, const std::string& seg, int maxWidth,
                            std::vector<std::string>& out) {
  std::string line;
  for (size_t i = 0; i < seg.size();) {
    size_t end = i + 1;
    while (end < seg.size() && !isUtf8Start((unsigned char)seg[end])) ++end;
    std::string candidate = line + seg.substr(i, end - i);
    if (!line.empty() && r.getTextWidth(fontId, candidate.c_str()) > maxWidth) {
      out.push_back(line);
      line = seg.substr(i, end - i);
    } else {
      line = std::move(candidate);
    }
    i = end;
  }
  if (!line.empty()) out.push_back(std::move(line));
}

// wordCut (optional) is set when some word had to be split by codepoint.
static void wrapSegment(const GfxRenderer& r, int fontId, const std::string& seg, int maxWidth,
                        std::vector<std::string>& out, bool* wordCut) {
  if (seg.empty()) {
    out.push_back("");
    return;
  }
  if (r.getTextWidth(fontId, seg.c_str()) <= maxWidth) {
    out.push_back(seg);
    return;
  }
  std::string line;
  size_t i = 0;
  while (i < seg.size()) {
    size_t wend = seg.find(' ', i);
    if (wend == std::string::npos) wend = seg.size();
    std::string word = seg.substr(i, wend - i);
    std::string candidate = line.empty() ? word : line + " " + word;
    if (r.getTextWidth(fontId, candidate.c_str()) <= maxWidth) {
      line = std::move(candidate);
    } else {
      if (!line.empty()) {
        out.push_back(std::move(line));
        line.clear();
      }
      if (r.getTextWidth(fontId, word.c_str()) > maxWidth) {
        if (wordCut) *wordCut = true;
        wrapByCodepoint(r, fontId, word, maxWidth, out);
      } else {
        line = std::move(word);
      }
    }
    i = (wend < seg.size()) ? wend + 1 : wend;
  }
  if (!line.empty()) out.push_back(std::move(line));
}

std::vector<std::string> wrapText(const GfxRenderer& r, int fontId, const std::string& text, int maxWidth,
                                  bool* wordCut) {
  std::vector<std::string> lines;
  size_t i = 0;
  while (i <= text.size()) {
    size_t nl = text.find('\n', i);
    if (nl == std::string::npos) nl = text.size();
    wrapSegment(r, fontId, text.substr(i, nl - i), maxWidth, lines, wordCut);
    if (nl == text.size()) break;
    i = nl + 1;
  }
  return lines;
}

// 5.2 — largest NotoSerif size that fits availH, uses at most 4 lines and
// never splits a word. Falls back to 14 (codepoint split allowed).
static constexpr int MAX_MAIN_LINES = 4;

// The 20-24 serif and all mono sizes are Latin-only subsets. A size is
// skipped when it lacks a glyph the full-coverage serif (14) has, so nothing
// the built-in fonts can draw ends up invisible. Codepoints no built-in font
// has (e.g. arrows) do not affect the choice.
static bool fontCovers(const GfxRenderer& r, int fontId, const std::string& text) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text.c_str());
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&p))) {
    if (cp < 0x20) continue;  // '\n' and other controls are never drawn
    if (!r.fontHasCodepoint(fontId, cp) && r.fontHasCodepoint(NOTOSERIF_14_FONT_ID, cp)) return false;
  }
  return true;
}

int pickMainFont(const GfxRenderer& r, const std::string& text, int maxWidth, int availH,
                 std::vector<std::string>& outLines) {
  const int candidates[] = {NOTOSERIF_24_FONT_ID, NOTOSERIF_22_FONT_ID, NOTOSERIF_20_FONT_ID,
                            NOTOSERIF_18_FONT_ID, NOTOSERIF_16_FONT_ID, NOTOSERIF_14_FONT_ID};
  for (int f : candidates) {
    if (!fontCovers(r, f, text)) continue;
    bool cut = false;
    auto lines = wrapText(r, f, text, maxWidth, &cut);
    const int needed = r.getLineHeight(f) * (int)lines.size();
    if (!cut && needed <= availH && (int)lines.size() <= MAX_MAIN_LINES) {
      outLines = std::move(lines);
      return f;
    }
  }
  outLines = wrapText(r, NOTOSERIF_14_FONT_ID, text, maxWidth);
  return NOTOSERIF_14_FONT_ID;
}

// 5.3 — back_content fully wrapped in backticks is drawn in JetBrains Mono.
bool isMonoAnswer(const std::string& back) { return back.size() >= 2 && back.front() == '`' && back.back() == '`'; }

// Split into tokens that end in '/' or ' ' (the delimiter stays in the token).
static std::vector<std::string> monoTokens(const std::string& text) {
  std::vector<std::string> tokens;
  std::string cur;
  for (char c : text) {
    cur += c;
    if (c == '/' || c == ' ') {
      tokens.push_back(std::move(cur));
      cur.clear();
    }
  }
  if (!cur.empty()) tokens.push_back(std::move(cur));
  return tokens;
}

// Greedy token packing; returns false if a single token is wider than maxWidth.
static bool packMonoTokens(const GfxRenderer& r, int fontId, const std::vector<std::string>& tokens, int maxWidth,
                           std::vector<std::string>& out) {
  out.clear();
  std::string line;
  for (const auto& tok : tokens) {
    std::string candidate = line + tok;
    // Trailing spaces don't count toward the width of a line.
    std::string visible = candidate;
    while (!visible.empty() && visible.back() == ' ') visible.pop_back();
    if (line.empty() || r.getTextWidth(fontId, visible.c_str()) <= maxWidth) {
      line = std::move(candidate);
    } else {
      out.push_back(line);
      line = tok;
    }
    std::string lineVisible = line;
    while (!lineVisible.empty() && lineVisible.back() == ' ') lineVisible.pop_back();
    if (r.getTextWidth(fontId, lineVisible.c_str()) > maxWidth) return false;
  }
  if (!line.empty()) out.push_back(line);
  for (auto& l : out) {
    while (!l.empty() && l.back() == ' ') l.pop_back();
  }
  return true;
}

int pickMonoFont(const GfxRenderer& r, const std::string& text, int maxWidth, int availH,
                 std::vector<std::string>& outLines) {
  // Mono sizes share one glyph subset; outside it, fall back to the serif rule.
  if (!fontCovers(r, JETBRAINSMONO_14_FONT_ID, text)) {
    return pickMainFont(r, text, maxWidth, availH, outLines);
  }
  const int oneLine[] = {JETBRAINSMONO_24_FONT_ID, JETBRAINSMONO_22_FONT_ID, JETBRAINSMONO_20_FONT_ID,
                         JETBRAINSMONO_18_FONT_ID, JETBRAINSMONO_16_FONT_ID, JETBRAINSMONO_14_FONT_ID};
  for (int f : oneLine) {
    if (r.getTextWidth(f, text.c_str()) <= maxWidth && r.getLineHeight(f) <= availH) {
      outLines = {text};
      return f;
    }
  }
  const auto tokens = monoTokens(text);
  const int wrapped[] = {JETBRAINSMONO_20_FONT_ID, JETBRAINSMONO_18_FONT_ID, JETBRAINSMONO_16_FONT_ID,
                         JETBRAINSMONO_14_FONT_ID};
  for (int f : wrapped) {
    std::vector<std::string> lines;
    if (packMonoTokens(r, f, tokens, maxWidth, lines) && r.getLineHeight(f) * (int)lines.size() <= availH) {
      outLines = std::move(lines);
      return f;
    }
  }
  // Last resort: a token wider than the screen even at 14.
  outLines.clear();
  wrapByCodepoint(r, JETBRAINSMONO_14_FONT_ID, text, maxWidth, outLines);
  return JETBRAINSMONO_14_FONT_ID;
}

FrontLayout layoutFront(const GfxRenderer& r, const std::string& main, const std::string& hint, const CardArea& area,
                        const int hintFont) {
  FrontLayout out;
  out.hintFont = hintFont;
  if (!hint.empty()) out.hintLines = wrapText(r, hintFont, hint, area.width);
  const int hintLineH = std::max(1, r.getLineHeight(hintFont));
  // A very long hint keeps at most half of the area, like the question on the back.
  const int maxHintShare = std::max(1, (area.bottom - area.top) / 2 / hintLineH);
  if ((int)out.hintLines.size() > maxHintShare) out.hintLines.resize(maxHintShare);
  const int hintReserve = out.hintLines.empty() ? 0 : hintLineH * (int)out.hintLines.size() + area.spacing;
  const int mainAvail = std::max(0, area.bottom - area.top - hintReserve);

  out.mainFont = pickMainFont(r, main, area.width, mainAvail, out.mainLines);
  const int mainLineH = std::max(1, r.getLineHeight(out.mainFont));
  // Text too long even at the smallest size: keep what fits (at least one
  // line), then give the hint whatever room is left. Nothing is drawn past
  // area.bottom, over the button hints.
  const int areaH = std::max(0, area.bottom - area.top);
  const int maxMainLines = std::max(1, mainAvail / mainLineH);
  if ((int)out.mainLines.size() > maxMainLines) out.mainLines.resize(maxMainLines);
  const int blockH = mainLineH * (int)out.mainLines.size();
  if (!out.hintLines.empty()) {
    const int hintRoom = areaH - blockH - area.spacing;
    const int maxHintLines = hintRoom > 0 ? hintRoom / hintLineH : 0;
    if ((int)out.hintLines.size() > maxHintLines) out.hintLines.resize(std::max(0, maxHintLines));
  }
  const int hintH = out.hintLines.empty() ? 0 : hintLineH * (int)out.hintLines.size() + area.spacing;
  out.mainY = area.top + std::max(0, (areaH - hintH - blockH) / 2);
  out.hintY = out.mainY + blockH + area.spacing;
  return out;
}

BackLayout layoutBack(const GfxRenderer& r, const std::string& question, const std::string& backContent,
                      const CardArea& area, const int questionFont) {
  BackLayout out;
  out.questionFont = questionFont;
  out.questionY = area.top;
  out.questionLines = wrapText(r, questionFont, question, area.width);
  const int qLineH = r.getLineHeight(questionFont);
  // A very long question keeps at most half of the area for itself.
  const int maxQuestionLines = std::max(1, (area.bottom - area.top) / 2 / std::max(1, qLineH));
  if ((int)out.questionLines.size() > maxQuestionLines) out.questionLines.resize(maxQuestionLines);

  int y = area.top + qLineH * (int)out.questionLines.size() + area.spacing;
  out.separatorY = y;
  y += area.spacing;

  const int answerTop = y;
  const int budget = std::max(0, area.bottom - answerTop);
  if (isMonoAnswer(backContent)) {
    out.answerFont =
        pickMonoFont(r, backContent.substr(1, backContent.size() - 2), area.width, budget, out.answerLines);
  } else {
    // Mono answers stay literal so paths like /etc/nginx keep their "/n".
    out.answerFont = pickMainFont(r, FlashcardDeck::decodeNewlines(backContent), area.width, budget, out.answerLines);
  }
  const int lineH = r.getLineHeight(out.answerFont);
  const int maxLines = lineH > 0 ? budget / lineH : 0;
  if ((int)out.answerLines.size() > maxLines) out.answerLines.resize(std::max(0, maxLines));
  const int blockH = lineH * (int)out.answerLines.size();
  out.answerY = answerTop + std::max(0, (budget - blockH) / 2);
  return out;
}

}  // namespace FlashcardLayout
