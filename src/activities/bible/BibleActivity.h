#pragma once
#include <string>
#include <vector>

#include "BibleFile.h"
#include "BibleLayout.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Hidden Bible reader (opened by holding side Up + front Right on Home):
// versions -> books -> chapter grid -> reading. In the lists a press moves one
// item and holding skips ten; while reading a press turns the page and holding
// moves to the next/previous chapter; holding Confirm picks the text size.
// Back goes up one level.
class BibleActivity final : public Activity {
  enum class Screen : uint8_t { Versions, Books, Chapters, Reading, FontSize };

  ButtonNavigator buttonNavigator;
  Screen screen = Screen::Versions;
  bool waitRelease = true;  // the buttons that opened it are still down

  std::vector<Bible::Version> versions;
  std::vector<std::string> versionTitles;
  std::vector<std::string> versionSubtitles;
  int versionSel = 0;

  Bible::File file;
  int bookSel = 0;
  int chapterSel = 0;

  // Open chapter: its text, laid out into lines and pages.
  int book = 0;
  int chapter = 0;
  std::string text;
  std::vector<BibleLayout::Line> lines;
  std::vector<uint16_t> pages;
  int page = 0;
  bool readError = false;
  bool positionDirty = false;
  int pagesUntilFullRefresh = 0;
  int fontIndex = 1;  // into FONT_SIZES; Normal
  int fontSel = 1;    // highlighted row on the FontSize screen

  void loadVersions();
  bool openVersion(int index);
  // verse > 0: the page showing that line of it; verse < 0: the last page.
  void openChapter(int bookIndex, int chapterIndex, int verse, int lineInVerse = 0);
  void moveChapter(int delta, bool lastPage);
  void savePosition();
  BibleLayout::Geometry geometry() const;
  int pageTop() const;
  int pageHeight() const;
  void stepList(int& index, int count);
  void loopReading();
  void loopFontSize();
  void applyFontSize(int index);

  void renderVersions();
  void renderBooks();
  void renderChapters();
  void renderReading();
  void renderFontSize();

 public:
  explicit BibleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Bible", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
