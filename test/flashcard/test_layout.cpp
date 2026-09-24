// Host-side check of card legibility (spec 5.2-5.4, acceptance 8) using the
// real built-in font metrics and FlashcardLayout. Geometry: 480x800 portrait,
// Lyra theme metrics (the device's uiTheme; tallest header of all themes).
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Utf8.h>
#include <builtinFonts/jetbrainsmono_14_regular.h>
#include <builtinFonts/jetbrainsmono_16_regular.h>
#include <builtinFonts/jetbrainsmono_18_regular.h>
#include <builtinFonts/jetbrainsmono_20_regular.h>
#include <builtinFonts/jetbrainsmono_22_regular.h>
#include <builtinFonts/jetbrainsmono_24_regular.h>
#include <builtinFonts/notoserif_14_regular.h>
#include <builtinFonts/notoserif_16_regular.h>
#include <builtinFonts/notoserif_18_regular.h>
#include <builtinFonts/notoserif_20_regular.h>
#include <builtinFonts/notoserif_22_regular.h>
#include <builtinFonts/notoserif_24_regular.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_regular.h>

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "FlashcardDeck.h"
#include "FlashcardLayout.h"
#include "fontIds.h"

static constexpr int SCREEN_W = 480, SCREEN_H = 800;
static constexpr int TOP_PAD = 5, HEADER_H = 84, VSPACE = 16, SIDE_PAD = 20, HINTS_H = 40;
static constexpr int CONTENT_TOP = TOP_PAD + HEADER_H + VSPACE;
static constexpr int CONTENT_BOTTOM = SCREEN_H - HINTS_H - VSPACE;
static constexpr int CONTENT_W = SCREEN_W - SIDE_PAD * 2;

#define FONT(var, data)  \
  EpdFont var##F(&data); \
  EpdFontFamily var(&var##F);
FONT(ns14, notoserif_14_regular)
FONT(ns16, notoserif_16_regular)
FONT(ns18, notoserif_18_regular)
FONT(ns20, notoserif_20_regular) FONT(ns22, notoserif_22_regular) FONT(ns24, notoserif_24_regular)
    FONT(jb14, jetbrainsmono_14_regular) FONT(jb16, jetbrainsmono_16_regular) FONT(jb18, jetbrainsmono_18_regular)
        FONT(jb20, jetbrainsmono_20_regular) FONT(jb22, jetbrainsmono_22_regular) FONT(jb24, jetbrainsmono_24_regular)
            FONT(ui12, ubuntu_12_regular) FONT(ui10, ubuntu_10_regular)

                static const char* fontName(int id) {
  switch (id) {
    case NOTOSERIF_14_FONT_ID:
      return "serif 14";
    case NOTOSERIF_16_FONT_ID:
      return "serif 16";
    case NOTOSERIF_18_FONT_ID:
      return "serif 18";
    case NOTOSERIF_20_FONT_ID:
      return "serif 20";
    case NOTOSERIF_22_FONT_ID:
      return "serif 22";
    case NOTOSERIF_24_FONT_ID:
      return "serif 24";
    case JETBRAINSMONO_14_FONT_ID:
      return "mono 14";
    case JETBRAINSMONO_16_FONT_ID:
      return "mono 16";
    case JETBRAINSMONO_18_FONT_ID:
      return "mono 18";
    case JETBRAINSMONO_20_FONT_ID:
      return "mono 20";
    case JETBRAINSMONO_22_FONT_ID:
      return "mono 22";
    case JETBRAINSMONO_24_FONT_ID:
      return "mono 24";
  }
  return "?";
}

static bool isMono(int id) {
  return id == JETBRAINSMONO_14_FONT_ID || id == JETBRAINSMONO_16_FONT_ID || id == JETBRAINSMONO_18_FONT_ID ||
         id == JETBRAINSMONO_20_FONT_ID || id == JETBRAINSMONO_22_FONT_ID || id == JETBRAINSMONO_24_FONT_ID;
}

static int failures = 0;
#define EXPECT(cond, msg)          \
  do {                             \
    if (!(cond)) {                 \
      ++failures;                  \
      printf("  FAIL: %s\n", msg); \
    }                              \
  } while (0)

struct Laid {
  int font;
  std::vector<std::string> lines;
  bool cut;
};

static const FlashcardLayout::CardArea AREA{CONTENT_TOP, CONTENT_BOTTOM, CONTENT_W, VSPACE};

static bool anyCut(GfxRenderer& r, int font, const std::string& text) {
  bool cut = false;
  FlashcardLayout::wrapText(r, font, text, CONTENT_W, &cut);
  return cut;
}

// Same calls FlashcardReviewActivity::renderCard makes.
static Laid layFront(GfxRenderer& r, const FlashcardCard& c) {
  std::string main, hint;
  c.parseFront(main, hint);
  const auto f = FlashcardLayout::layoutFront(r, main, hint, AREA, UI_10_FONT_ID);
  const int bottom = f.hintLines.empty() ? f.mainY + r.getLineHeight(f.mainFont) * (int)f.mainLines.size()
                                         : f.hintY + r.getLineHeight(f.hintFont) * (int)f.hintLines.size();
  EXPECT(f.mainY >= CONTENT_TOP && bottom <= CONTENT_BOTTOM, "front fits the card area");
  for (auto& line : f.mainLines) EXPECT(r.getTextWidth(f.mainFont, line.c_str()) <= CONTENT_W, "front line too wide");
  return {f.mainFont, f.mainLines, anyCut(r, f.mainFont, main)};
}

static Laid layBack(GfxRenderer& r, const FlashcardCard& c) {
  const auto b = FlashcardLayout::layoutBack(r, c.frontMain(), c.backContent, AREA, UI_12_FONT_ID);
  const int lineH = r.getLineHeight(b.answerFont);
  EXPECT(b.separatorY > b.questionY && b.answerY > b.separatorY, "back order: question, separator, answer");
  EXPECT(b.answerY + lineH * (int)b.answerLines.size() <= CONTENT_BOTTOM, "answer inside card area");
  for (auto& line : b.answerLines)
    EXPECT(r.getTextWidth(b.answerFont, line.c_str()) <= CONTENT_W, "answer line too wide");
  const bool mono = FlashcardLayout::isMonoAnswer(c.backContent);
  return {b.answerFont, b.answerLines, !mono && anyCut(r, b.answerFont, FlashcardDeck::decodeNewlines(c.backContent))};
}

static void print(const char* side, const Laid& l) {
  printf("  %-5s %-8s %zu line(s)%s\n", side, fontName(l.font), l.lines.size(), l.cut ? " WORD CUT" : "");
  for (const auto& s : l.lines) printf("        | %s\n", s.c_str());
}

int main(int argc, char** argv) {
  Storage.root = argc > 1 ? argv[1] : ".";
  GfxRenderer r;
  r.insertFont(NOTOSERIF_14_FONT_ID, ns14);
  r.insertFont(NOTOSERIF_16_FONT_ID, ns16);
  r.insertFont(NOTOSERIF_18_FONT_ID, ns18);
  r.insertFont(NOTOSERIF_20_FONT_ID, ns20);
  r.insertFont(NOTOSERIF_22_FONT_ID, ns22);
  r.insertFont(NOTOSERIF_24_FONT_ID, ns24);
  r.insertFont(JETBRAINSMONO_14_FONT_ID, jb14);
  r.insertFont(JETBRAINSMONO_16_FONT_ID, jb16);
  r.insertFont(JETBRAINSMONO_18_FONT_ID, jb18);
  r.insertFont(JETBRAINSMONO_20_FONT_ID, jb20);
  r.insertFont(JETBRAINSMONO_22_FONT_ID, jb22);
  r.insertFont(JETBRAINSMONO_24_FONT_ID, jb24);
  r.insertFont(UI_12_FONT_ID, ui12);
  r.insertFont(UI_10_FONT_ID, ui10);

  FlashcardDeck deck;
  if (!deck.loadFromCsv("/lpic_test.csv")) {
    printf("cannot load deck\n");
    return 1;
  }
  const auto& cards = deck.getCards();
  std::vector<Laid> backs;
  for (size_t i = 0; i < cards.size(); ++i) {
    printf("card %zu\n", i + 1);
    Laid f = layFront(r, cards[i]);
    Laid b = layBack(r, cards[i]);
    print("front", f);
    print("back", b);
    if (i < 6) EXPECT(!f.cut && !b.cut, "word cut in cards 1-6");
    backs.push_back(b);
  }

  // Acceptance 8
  EXPECT(layFront(r, cards[1]).font == NOTOSERIF_24_FONT_ID || layFront(r, cards[0]).font == NOTOSERIF_24_FONT_ID,
         "short card at 24");
  EXPECT(backs[0].lines.size() == 1 && isMono(backs[0].font), "wc -l notas.txt: mono, 1 line");
  EXPECT(isMono(backs[1].font) && isMono(backs[2].font) && isMono(backs[5].font), "cards 2, 3, 6 back in mono");
  EXPECT(!isMono(backs[3].font) && !isMono(backs[4].font) && !isMono(backs[6].font), "cards 4, 5, 7 back in serif");
  EXPECT(backs[1].lines.size() == 1, "/etc/fstab: mono, 1 line");
  EXPECT(backs[2].lines.size() > 1, "long path wraps");
  for (size_t k = 0; k + 1 < backs[2].lines.size(); ++k)
    EXPECT(!backs[2].lines[k].empty() && backs[2].lines[k].back() == '/', "long path breaks after /");
  EXPECT(backs[3].lines.size() <= 4, "card 4 back at most 4 lines");
  EXPECT(backs[5].lines.size() >= 1 && backs[5].lines[0].find('\\') != std::string::npos, "card 6 literal backslash");
  EXPECT(backs[6].lines.size() == 2, "card 7 back: two lines");
  EXPECT(layFront(r, cards[6]).lines.size() >= 2 && layFront(r, cards[6]).lines[0] == "Quebra", "card 7 front newline");

  // Stress cases
  auto card = [](const char* front, const char* back) {
    FlashcardCard c;
    c.frontContent = front;
    c.backContent = back;
    return c;
  };
  auto covered = [&r](int font, const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
      const unsigned char* p = reinterpret_cast<const unsigned char*>(line.c_str());
      uint32_t cp;
      while ((cp = utf8NextCodepoint(&p))) {
        if (cp >= 0x20 && !r.fontHasCodepoint(font, cp) && r.fontHasCodepoint(NOTOSERIF_14_FONT_ID, cp)) return false;
      }
    }
    return true;
  };
  {
    // Cyrillic / Latin Extended-A are in serif 14-18 but not in the Latin-only
    // 20-24 serif and mono subsets: the layout must pick a size that has them.
    const auto f = layFront(r, card("Qual a letra \u0416?", "x"));
    EXPECT(covered(f.font, f.lines), "front with Cyrillic drawn with a font that has it");
    const auto b = layBack(r, card("q", "`Erd\u0151s`"));
    EXPECT(covered(b.font, b.lines) && !isMono(b.font), "mono answer with a non-subset letter falls back to serif");
    // No built-in font has an arrow: it must not push a short card down to 14.
    const auto arrow = layFront(r, card("A \u2192 B", "x"));
    EXPECT(arrow.font == NOTOSERIF_24_FONT_ID, "undrawable arrow does not shrink the card");
  }
  {
    std::string longAnswer;
    for (int i = 0; i < 60; ++i) longAnswer += "palavra ";
    const auto b = layBack(r, card("Pergunta curta", longAnswer.c_str()));  // bounds checked inside layBack
    EXPECT(!b.lines.empty(), "long answer still shows lines");
    // Huge front and hint stay inside the card area (checked in layFront)
    std::string hugeFront, hugeHint;
    for (int i = 0; i < 120; ++i) hugeFront += "frente ";
    for (int i = 0; i < 120; ++i) hugeHint += "dica ";
    const std::string huge = hugeFront + "\\ " + hugeHint + " \\";
    const auto hf = layFront(r, card(huge.c_str(), "x"));
    EXPECT(!hf.lines.empty(), "huge front still shows lines");
    std::string longQuestion;
    for (int i = 0; i < 80; ++i) longQuestion += "pergunta ";
    const auto q = layBack(r, card(longQuestion.c_str(), "`/etc/fstab`"));
    EXPECT(q.lines.size() == 1, "long question leaves room for the answer");
    const auto longPath =
        layBack(r, card("q", "`/a/very/long/path/that/keeps/going/and/going/and/going/forever/and/ever.conf`"));
    EXPECT(!longPath.lines.empty(), "long mono path shows lines");
  }

  printf("layout: %d failures\n", failures);
  return failures ? 1 : 0;
}
