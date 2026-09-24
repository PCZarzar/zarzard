// Checks real decks against the on-device card layout (Lyra theme, 480x800).
// Usage: bash test/flashcard/check_deck.sh deck.csv [deck2.csv ...]
// Flags cards whose text would be cut (word split or lines dropped), drawn
// with missing glyphs, or shrunk to the smallest size.
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
#include <string>
#include <vector>

#include "FlashcardDeck.h"
#include "FlashcardLayout.h"
#include "fontIds.h"

static constexpr int SCREEN_W = 480, SCREEN_H = 800;
static constexpr int TOP_PAD = 5, HEADER_H = 84, VSPACE = 16, SIDE_PAD = 20, HINTS_H = 40;
static const FlashcardLayout::CardArea AREA{TOP_PAD + HEADER_H + VSPACE, SCREEN_H - HINTS_H - VSPACE,
                                            SCREEN_W - SIDE_PAD * 2, VSPACE};

#define FONT(var, data)  \
  EpdFont var##F(&data); \
  EpdFontFamily var(&var##F);
FONT(ns14, notoserif_14_regular)
FONT(ns16, notoserif_16_regular) FONT(ns18, notoserif_18_regular) FONT(ns20, notoserif_20_regular)
    FONT(ns22, notoserif_22_regular) FONT(ns24, notoserif_24_regular) FONT(jb14, jetbrainsmono_14_regular)
        FONT(jb16, jetbrainsmono_16_regular) FONT(jb18, jetbrainsmono_18_regular) FONT(jb20, jetbrainsmono_20_regular)
            FONT(jb22, jetbrainsmono_22_regular) FONT(jb24, jetbrainsmono_24_regular) FONT(ui12, ubuntu_12_regular)
                FONT(ui10, ubuntu_10_regular)

                    static const char* fontName(int id) {
  struct {
    int id;
    const char* n;
  } t[] = {
      {NOTOSERIF_14_FONT_ID, "serif14"},    {NOTOSERIF_16_FONT_ID, "serif16"},    {NOTOSERIF_18_FONT_ID, "serif18"},
      {NOTOSERIF_20_FONT_ID, "serif20"},    {NOTOSERIF_22_FONT_ID, "serif22"},    {NOTOSERIF_24_FONT_ID, "serif24"},
      {JETBRAINSMONO_14_FONT_ID, "mono14"}, {JETBRAINSMONO_16_FONT_ID, "mono16"}, {JETBRAINSMONO_18_FONT_ID, "mono18"},
      {JETBRAINSMONO_20_FONT_ID, "mono20"}, {JETBRAINSMONO_22_FONT_ID, "mono22"}, {JETBRAINSMONO_24_FONT_ID, "mono24"}};
  for (auto& e : t)
    if (e.id == id) return e.n;
  return "?";
}

static bool hasMissingGlyph(GfxRenderer& r, int font, const std::vector<std::string>& lines) {
  for (const auto& line : lines) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(line.c_str());
    uint32_t cp;
    while ((cp = utf8NextCodepoint(&p)))
      if (cp >= 0x20 && !r.fontHasCodepoint(font, cp)) return true;
  }
  return false;
}

int main(int argc, char** argv) {
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

  int problems = 0;
  Storage.root = "";
  for (int a = 1; a < argc; ++a) {
    FlashcardDeck deck;
    if (!deck.loadFromCsv(argv[a])) {
      printf("%s: FAILED TO LOAD\n", argv[a]);
      ++problems;
      continue;
    }
    printf("== %s: %zu cards%s\n", argv[a], deck.getCards().size(), deck.isTruncated() ? " (TRUNCATED)" : "");
    for (const auto& c : deck.getCards()) {
      std::string main, hint;
      c.parseFront(main, hint);
      const auto f = FlashcardLayout::layoutFront(r, main, hint, AREA, UI_10_FONT_ID);
      const auto b = FlashcardLayout::layoutBack(r, main, c.backContent, AREA, UI_12_FONT_ID);
      const bool mono = FlashcardLayout::isMonoAnswer(c.backContent);
      std::vector<std::string> full;  // unclipped answer, to detect dropped lines / split words
      bool cut = false;
      const std::string answer =
          mono ? c.backContent.substr(1, c.backContent.size() - 2) : FlashcardDeck::decodeNewlines(c.backContent);
      if (mono)
        FlashcardLayout::pickMonoFont(r, answer, AREA.width, 10000, full);
      else
        full = FlashcardLayout::wrapText(r, b.answerFont, answer, AREA.width, &cut);
      bool frontCut = false;
      FlashcardLayout::wrapText(r, f.mainFont, main, AREA.width, &frontCut);

      std::string notes;
      if (frontCut) notes += " FRONT-WORD-SPLIT";
      if (!mono && cut) notes += " BACK-WORD-SPLIT";
      if (mono && b.answerLines.size() > 1 && b.answerFont == JETBRAINSMONO_14_FONT_ID) {
        for (const auto& l : b.answerLines)
          if (r.getTextWidth(b.answerFont, l.c_str()) > AREA.width) notes += " MONO-OVERFLOW";
      }
      if (b.answerLines.size() < full.size() && !mono) notes += " BACK-LINES-DROPPED";
      if (hasMissingGlyph(r, f.mainFont, f.mainLines) || hasMissingGlyph(r, b.answerFont, b.answerLines))
        notes += " MISSING-GLYPH";
      if (b.questionLines.size() < FlashcardLayout::wrapText(r, UI_12_FONT_ID, main, AREA.width).size())
        notes += " QUESTION-TRUNCATED";
      if (!notes.empty()) ++problems;
      printf("  #%-3u front %-7s %zuL%s | back %-7s %zuL%s\n", (unsigned)c.id, fontName(f.mainFont), f.mainLines.size(),
             hint.empty() ? "" : " +hint", fontName(b.answerFont), b.answerLines.size(), notes.c_str());
    }
  }
  printf("%d card(s) with problems\n", problems);
  return problems ? 1 : 0;
}
