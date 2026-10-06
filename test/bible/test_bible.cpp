// Host checks of the Bible reader against real .zbib files and the real fonts:
// file parsing, every chapter's layout and pagination, glyph coverage, the
// saved position, and rejection of damaged files.
// Usage: test_bible <work dir> <dir with .zbib files>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Utf8.h>
#include <builtinFonts/manrope_10_regular.h>
#include <builtinFonts/manrope_12_bold.h>
#include <builtinFonts/manrope_12_regular.h>
#include <builtinFonts/manrope_8_regular.h>
#include <builtinFonts/notoserif_12_regular.h>
#include <builtinFonts/notoserif_14_regular.h>
#include <builtinFonts/notoserif_16_regular.h>
#include <builtinFonts/notoserif_18_regular.h>
#include <builtinFonts/notoserif_20_regular.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "BibleFile.h"
#include "BibleLayout.h"
#include "fontIds.h"

namespace fs = std::filesystem;

static int checks = 0, failures = 0;
#define CHECK(cond, ...)                           \
  do {                                             \
    ++checks;                                      \
    if (!(cond)) {                                 \
      ++failures;                                  \
      if (failures <= 40) {                        \
        printf("FAIL %s:%d ", __FILE__, __LINE__); \
        printf(__VA_ARGS__);                       \
        printf("\n");                              \
      }                                            \
    }                                              \
  } while (0)

// Device geometry (Zarzard theme, 480x800 portrait), as in BibleActivity.
static constexpr int SCREEN_W = 480, SCREEN_H = 800;
static constexpr int TOP_PAD = 5, HEADER_H = 84, VSPACE = 16, SIDE_PAD = 24, HINTS_H = 40;
static constexpr int TEXT_MARGIN = 20, NUMBER_GAP = 6, VERSE_GAP = 4;

EpdFont serif12F(&notoserif_12_regular), serifF(&notoserif_14_regular), serif16F(&notoserif_16_regular),
    serif18F(&notoserif_18_regular), serif20F(&notoserif_20_regular);
EpdFontFamily serif12(&serif12F), serif(&serifF), serif16(&serif16F), serif18(&serif18F), serif20(&serif20F);
// Text and verse-number fonts of each size, as in BibleActivity::FONT_SIZES.
static const int SIZES[][2] = {{NOTOSERIF_12_FONT_ID, SMALL_FONT_ID},
                               {NOTOSERIF_14_FONT_ID, SMALL_FONT_ID},
                               {NOTOSERIF_16_FONT_ID, UI_10_FONT_ID},
                               {NOTOSERIF_18_FONT_ID, UI_10_FONT_ID},
                               {NOTOSERIF_20_FONT_ID, UI_12_FONT_ID}};
EpdFont smallF(&manrope_8_regular);
EpdFontFamily small(&smallF);
EpdFont ui10F(&manrope_10_regular);
EpdFontFamily ui10(&ui10F);
EpdFont ui12F(&manrope_12_regular), ui12bF(&manrope_12_bold);
EpdFontFamily ui12(&ui12F, &ui12bF);

static std::vector<uint32_t> codepoints(const std::string& s) {
  std::vector<uint32_t> out;
  const auto* p = reinterpret_cast<const unsigned char*>(s.c_str());
  while (*p) out.push_back(utf8NextCodepoint(&p));
  return out;
}

static void checkGlyphs(const GfxRenderer& r, int font, const char* fontName, const std::string& s,
                        std::set<uint32_t>& reported) {
  for (const uint32_t cp : codepoints(s)) {
    if (cp == '\n' || r.fontHasCodepoint(font, cp) || reported.count(cp)) continue;
    reported.insert(cp);
    CHECK(false, "U+%04X missing in %s (in \"%.60s\")", cp, fontName, s.c_str());
  }
}

static void writeBytes(const std::string& path, const std::string& bytes) {
  std::ofstream(path, std::ios::binary) << bytes;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    printf("usage: test_bible <work dir> <zbib dir>\n");
    return 2;
  }
  const std::string work = argv[1];
  Storage.root = work;
  fs::create_directories(work + "/biblia");
  fs::create_directories(work + "/.crosspoint");
  for (const auto& e : fs::directory_iterator(argv[2])) {
    if (e.path().extension() == ".zbib") fs::copy_file(e.path(), work + "/biblia/" + e.path().filename().string());
  }

  GfxRenderer r;
  r.insertFont(NOTOSERIF_12_FONT_ID, serif12);
  r.insertFont(NOTOSERIF_14_FONT_ID, serif);
  r.insertFont(NOTOSERIF_16_FONT_ID, serif16);
  r.insertFont(NOTOSERIF_18_FONT_ID, serif18);
  r.insertFont(NOTOSERIF_20_FONT_ID, serif20);
  r.insertFont(SMALL_FONT_ID, small);
  r.insertFont(UI_10_FONT_ID, ui10);
  r.insertFont(UI_12_FONT_ID, ui12);

  const int pageTop = TOP_PAD + HEADER_H;
  const int footer = r.getLineHeight(SMALL_FONT_ID) + VSPACE;
  const int pageH = SCREEN_H - pageTop - HINTS_H - VSPACE - footer;

  // ── Versions found ──
  const auto versions = Bible::listVersions();
  CHECK(versions.size() == 3, "expected 3 versions, got %zu", versions.size());
  std::set<std::string> labels;
  for (const auto& v : versions) labels.insert(v.label);
  CHECK(labels == std::set<std::string>({"ARA", "KJV", "NVI"}), "labels");

  std::set<uint32_t> reported;
  size_t totalPages = 0, maxPages = 0, maxLines = 0, maxText = 0;
  for (const auto& v : versions) {
    Bible::File file;
    CHECK(file.open(v.path), "open %s", v.path.c_str());
    CHECK(file.label() == v.label, "label");
    const auto& books = file.books();
    CHECK(books.size() == 66, "%s books %zu", v.label.c_str(), books.size());
    int chapters = 0;
    for (const auto& b : books) {
      chapters += b.chapters;
      checkGlyphs(r, UI_12_FONT_ID, "manrope 12", b.name, reported);
      checkGlyphs(r, SMALL_FONT_ID, "manrope 8", b.abbrev, reported);
      CHECK(r.getTextWidth(UI_12_FONT_ID, b.name.c_str()) < SCREEN_W - 2 * SIDE_PAD - 40, "book name too wide: %s",
            b.name.c_str());
    }
    CHECK(chapters == 1189, "%s chapters %d", v.label.c_str(), chapters);
    checkGlyphs(r, UI_12_FONT_ID, "manrope 12", v.fullName, reported);

    std::string text;
    for (int bi = 0; bi < static_cast<int>(books.size()); ++bi) {
      for (int ci = 0; ci < books[bi].chapters; ++ci) {
        const bool ok = file.readChapter(bi, ci, text);
        CHECK(ok, "%s read %s %d", v.label.c_str(), books[bi].name.c_str(), ci + 1);
        if (!ok) continue;
        maxText = std::max(maxText, text.size());
        CHECK(text.find('\r') == std::string::npos, "CR in text");
        for (const auto& size : SIZES) checkGlyphs(r, size[0], "noto serif", text, reported);

        for (const auto& size : SIZES) {
          BibleLayout::Geometry g;
          g.left = TEXT_MARGIN;
          g.width = SCREEN_W - 2 * TEXT_MARGIN;
          g.textFont = size[0];
          g.numberFont = size[1];
          g.numberGap = NUMBER_GAP;
          const int lineH = r.getLineHeight(g.textFont);
          const auto lines = BibleLayout::layout(r, text, g);
          const auto pages = BibleLayout::paginate(lines, lineH, VERSE_GAP, pageH);
          maxLines = std::max(maxLines, lines.size());
          totalPages += pages.size();
          maxPages = std::max(maxPages, pages.size());

          // Lines rebuild every verse exactly, numbered 1..n, within the width.
          std::vector<std::string> verses;
          for (size_t p = 0, e; p < text.size(); p = e + 1) {
            e = text.find('\n', p);
            verses.push_back(text.substr(p, e - p));
          }
          std::string rebuilt;
          int verse = 0;
          for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            const std::string s = text.substr(l.start, l.length);
            if (l.verse != 0) {
              CHECK(l.verse == verse + 1, "verse order");
              verse = l.verse;
              rebuilt = s;
            } else {
              rebuilt += " " + s;
            }
            const int w = r.getTextWidth(g.textFont, s.c_str());
            CHECK(w <= g.left + g.width - l.x || s.find(' ') == std::string::npos, "%s %s %d: line too wide (%d)",
                  v.label.c_str(), books[bi].name.c_str(), ci + 1, w);
            if (l.endsVerse) CHECK(verse >= 1 && rebuilt == verses[verse - 1], "verse %d rebuilt", verse);
          }
          CHECK(verse == static_cast<int>(verses.size()), "%s %s %d: %d of %zu verses laid out", v.label.c_str(),
                books[bi].name.c_str(), ci + 1, verse, verses.size());

          // Pages: start at 0, increase, each fits the text area.
          CHECK(!pages.empty() && pages[0] == 0, "first page");
          for (size_t p = 0; p < pages.size(); ++p) {
            const size_t end = p + 1 < pages.size() ? pages[p + 1] : lines.size();
            CHECK(end > pages[p], "empty page");
            int y = 0;
            for (size_t i = pages[p]; i < end; ++i) y += lineH + (lines[i].endsVerse && i + 1 < end ? VERSE_GAP : 0);
            CHECK(y <= pageH || end - pages[p] == 1, "%s %s %d page %zu overflows (%d > %d)", v.label.c_str(),
                  books[bi].name.c_str(), ci + 1, p + 1, y, pageH);
            // Resuming from the saved verse and line shows this same page.
            const int saved = BibleLayout::verseAtLine(lines, pages[p]);
            const int offset = pages[p] - BibleLayout::firstLineOfVerse(lines, saved);
            const int back = BibleLayout::pageOfVerse(lines, pages, saved, offset);
            CHECK(offset >= 0 && back == static_cast<int>(p), "%s %s %d: resume page %d for page %zu (verse %d)",
                  v.label.c_str(), books[bi].name.c_str(), ci + 1, back, p, saved);
          }
        }
      }
    }
    CHECK(!file.readChapter(66, 0, text) && !file.readChapter(0, 50, text) && !file.readChapter(-1, 0, text),
          "out-of-range chapters rejected");
  }
  printf("pages: %zu in all versions, longest chapter %zu pages / %zu lines, biggest chapter %zu bytes\n", totalPages,
         maxPages, maxLines, maxText);

  // ── Saved position ──
  Bible::Position pos;
  CHECK(!Bible::loadPosition("ARA", pos), "no position yet");
  pos.book = 18;
  pos.chapter = 22;
  pos.verse = 4;
  pos.line = 2;
  pos.where = "Salmos 23";
  CHECK(Bible::savePosition("ARA", pos), "save ARA");
  Bible::Position kjv;
  kjv.book = 42;
  kjv.chapter = 2;
  kjv.verse = 16;
  kjv.where = "John 3";
  CHECK(Bible::savePosition("KJV", kjv), "save KJV");
  Bible::Position back;
  CHECK(Bible::loadPosition("ARA", back) && back.book == 18 && back.chapter == 22 && back.verse == 4 &&
            back.line == 2 && back.where == "Salmos 23",
        "ARA position kept after saving KJV");
  CHECK(Bible::loadLastVersion() == "KJV", "last version");
  // A save cut before the rename leaves only the .tmp: it is recovered.
  fs::rename(work + "/.crosspoint/bible.json", work + "/.crosspoint/bible.json.tmp");
  CHECK(Bible::loadPosition("KJV", back) && back.verse == 16, ".tmp recovered");
  CHECK(fs::exists(work + "/.crosspoint/bible.json"), ".tmp renamed back");

  // ── Damaged files ──
  std::string good;
  {
    std::ifstream in(work + "/biblia/ARA.zbib", std::ios::binary);
    good.assign(std::istreambuf_iterator<char>(in), {});
  }
  Bible::File f;
  writeBytes(work + "/bad.zbib", good.substr(0, 1000));
  CHECK(!f.open("/bad.zbib"), "truncated table rejected");
  std::string magic = good;
  magic[0] = 'X';
  writeBytes(work + "/bad.zbib", magic);
  CHECK(!f.open("/bad.zbib"), "bad magic rejected");
  std::string ver = good;
  ver[4] = 9;
  writeBytes(work + "/bad.zbib", ver);
  CHECK(!f.open("/bad.zbib"), "unknown format version rejected");
  std::string order = good;
  order[92 + 44 + 40] = 7;  // second book's first chapter
  writeBytes(work + "/bad.zbib", order);
  CHECK(!f.open("/bad.zbib"), "broken book table rejected");
  writeBytes(work + "/bad.zbib", good.substr(0, good.size() - 5000));
  CHECK(f.open("/bad.zbib"), "header of a cut file still opens");
  std::string text;
  const auto& books = f.books();
  CHECK(!f.readChapter(65, books[65].chapters - 1, text), "chapter past the end of a cut file rejected");
  CHECK(f.readChapter(0, 0, text), "earlier chapters still read");
  CHECK(!Bible::File().readChapter(0, 0, text), "unopened file reads nothing");

  // Non-.zbib files and folders in /biblia are ignored.
  writeBytes(work + "/biblia/notes.txt", "x");
  fs::create_directories(work + "/biblia/old");
  CHECK(Bible::listVersions().size() == 3, "only .zbib files listed");

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
