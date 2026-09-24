// Host-side check of card legibility (spec 5.2-5.4, acceptance 8) using the
// real built-in font metrics and FlashcardLayout. Geometry: 480x800 portrait,
// Lyra theme metrics (the device's uiTheme; tallest header of all themes).
#include <GfxRenderer.h>
#include <HalStorage.h>
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
FONT(ns16, notoserif_16_regular) FONT(ns18, notoserif_18_regular) FONT(ns20, notoserif_20_regular)
    FONT(ns22, notoserif_22_regular) FONT(ns24, notoserif_24_regular) FONT(jb14, jetbrainsmono_14_regular)
        FONT(jb16, jetbrainsmono_16_regular) FONT(jb18, jetbrainsmono_18_regular) FONT(jb20, jetbrainsmono_20_regular)
            FONT(jb22, jetbrainsmono_22_regular) FONT(jb24, jetbrainsmono_24_regular) FONT(ui12, ubuntu_12_regular)

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

static Laid layFront(GfxRenderer& r, const FlashcardCard& c) {
  std::string main, hint;
  c.parseFront(main, hint);
  int hintH = 0;
  if (!hint.empty())
    hintH = r.getLineHeight(UI_12_FONT_ID) * (int)FlashcardLayout::wrapText(r, UI_12_FONT_ID, hint, CONTENT_W).size() +
            VSPACE;
  Laid l;
  l.font = FlashcardLayout::pickMainFont(r, main, CONTENT_W, CONTENT_BOTTOM - CONTENT_TOP - hintH, l.lines);
  l.cut = false;
  FlashcardLayout::wrapText(r, l.font, main, CONTENT_W, &l.cut);
  return l;
}

static Laid layBack(GfxRenderer& r, const FlashcardCard& c) {
  int y = CONTENT_TOP + r.getLineHeight(UI_12_FONT_ID) *
                            (int)FlashcardLayout::wrapText(r, UI_12_FONT_ID, c.frontMain(), CONTENT_W).size();
  y += VSPACE * 2;
  const int budget = CONTENT_BOTTOM - y;
  Laid l;
  l.cut = false;
  if (FlashcardLayout::isMonoAnswer(c.backContent)) {
    l.font =
        FlashcardLayout::pickMonoFont(r, c.backContent.substr(1, c.backContent.size() - 2), CONTENT_W, budget, l.lines);
  } else {
    const std::string text = FlashcardDeck::decodeNewlines(c.backContent);
    l.font = FlashcardLayout::pickMainFont(r, text, CONTENT_W, budget, l.lines);
    FlashcardLayout::wrapText(r, l.font, text, CONTENT_W, &l.cut);
  }
  for (auto& line : l.lines) EXPECT(r.getTextWidth(l.font, line.c_str()) <= CONTENT_W, "line wider than screen");
  EXPECT(r.getLineHeight(l.font) * (int)l.lines.size() <= budget, "answer taller than area");
  return l;
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

  printf("layout: %d failures\n", failures);
  return failures ? 1 : 0;
}
