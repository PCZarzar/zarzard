#include "FlashcardLayout.h"

#include <GfxRenderer.h>
#include <Utf8.h>

#include "fontIds.h"

namespace FlashcardLayout {

// ─── Text wrapping helpers ───────────────────────────────────────────────────
// Wrap plain text so each line fits within maxWidth pixels. Breaks at spaces
// when possible; for space-less scripts (CJK) or over-long words falls back to
// UTF-8 codepoint boundaries so multi-byte characters never get split mid-byte.

static bool isUtf8Start(unsigned char b) { return (b & 0xC0) != 0x80; }

static void wrapByCodepoint(GfxRenderer& r, int fontId, const std::string& seg, int maxWidth,
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
static void wrapSegment(GfxRenderer& r, int fontId, const std::string& seg, int maxWidth, std::vector<std::string>& out,
                        bool* wordCut) {
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

std::vector<std::string> wrapText(GfxRenderer& r, int fontId, const std::string& text, int maxWidth, bool* wordCut) {
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

// The 20-24 serif and all mono sizes are Latin-only subsets; a size that lacks
// a glyph of the text is skipped so nothing renders invisible.
static bool fontCovers(GfxRenderer& r, int fontId, const std::string& text) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(text.c_str());
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&p))) {
    if (cp < 0x20) continue;  // '\n' and other controls are never drawn
    if (!r.fontHasCodepoint(fontId, cp)) return false;
  }
  return true;
}

int pickMainFont(GfxRenderer& r, const std::string& text, int maxWidth, int availH,
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
static bool packMonoTokens(GfxRenderer& r, int fontId, const std::vector<std::string>& tokens, int maxWidth,
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

int pickMonoFont(GfxRenderer& r, const std::string& text, int maxWidth, int availH,
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

}  // namespace FlashcardLayout
