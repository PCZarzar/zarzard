#include "BibleActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "BibleView.h"
#include "activities/flashcard/FlashcardUi.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int JUMP = 10;  // items skipped per repeat while a button is held
constexpr int OLD_TESTAMENT_BOOKS = 39;
constexpr int FULL_CANON_BOOKS = 66;  // testament headings only for the 66-book canon
constexpr int VERSE_GAP = 4;
constexpr int NUMBER_GAP = 6;
constexpr int TEXT_INSET = 4;  // reading text sits a little inside the list margin
}  // namespace

using Button = MappedInputManager::Button;

void BibleActivity::onEnter() {
  Activity::onEnter();
  waitRelease = true;
  loadVersions();
  requestUpdate();
}

void BibleActivity::onExit() {
  savePosition();
  Activity::onExit();
}

void BibleActivity::loadVersions() {
  versions = Bible::listVersions();
  versionTitles.clear();
  versionSubtitles.clear();
  versionTitles.reserve(versions.size());
  versionSubtitles.reserve(versions.size());
  const std::string last = Bible::loadLastVersion();
  versionSel = 0;
  for (size_t i = 0; i < versions.size(); ++i) {
    const auto& v = versions[i];
    versionTitles.push_back(v.fullName.empty() ? v.label : v.fullName);
    // "ARA · Continue at Psalms 23", or just the label before any reading.
    std::string sub = v.label;
    Bible::Position pos;
    if (Bible::loadPosition(v.label, pos) && !pos.where.empty()) {
      sub += std::string(" \xC2\xB7 ") + tr(STR_ZZ_BIBLE_CONTINUE_AT) + " " + pos.where;
    }
    versionSubtitles.push_back(std::move(sub));
    if (v.label == last) versionSel = static_cast<int>(i);
  }
}

bool BibleActivity::openVersion(const int index) {
  if (index < 0 || index >= static_cast<int>(versions.size())) return false;
  savePosition();
  text.clear();
  lines.clear();
  pages.clear();
  if (!file.open(versions[index].path)) return false;
  bookSel = 0;
  chapterSel = 0;
  book = -1;
  return true;
}

BibleLayout::Geometry BibleActivity::geometry() const {
  const auto& m = UITheme::getInstance().getMetrics();
  BibleLayout::Geometry g;
  g.left = m.contentSidePadding + TEXT_INSET;
  g.width = renderer.getScreenWidth() - 2 * g.left;
  g.textFont = NOTOSERIF_14_FONT_ID;
  g.numberFont = SMALL_FONT_ID;
  g.numberGap = NUMBER_GAP;
  return g;
}

int BibleActivity::pageTop() const {
  const auto& m = UITheme::getInstance().getMetrics();
  return m.topPadding + m.headerHeight;
}

// Text area: below the header, above the footer line and the button hints.
int BibleActivity::pageHeight() const {
  const auto& m = UITheme::getInstance().getMetrics();
  const int footer = renderer.getLineHeight(SMALL_FONT_ID) + m.verticalSpacing;
  return renderer.getScreenHeight() - pageTop() - m.buttonHintsHeight - m.verticalSpacing - footer;
}

void BibleActivity::openChapter(const int bookIndex, const int chapterIndex, const int verse, const int lineInVerse) {
  book = bookIndex;
  chapter = chapterIndex;
  readError = !file.readChapter(book, chapter, text);
  lines.clear();
  pages.clear();
  if (!readError) {
    lines = BibleLayout::layout(renderer, text, geometry());
    pages = BibleLayout::paginate(lines, renderer.getLineHeight(NOTOSERIF_14_FONT_ID), VERSE_GAP, pageHeight());
  } else {
    pages.push_back(0);
  }
  if (verse < 0) {
    page = static_cast<int>(pages.size()) - 1;
  } else if (verse > 1 || lineInVerse > 0) {
    page = BibleLayout::pageOfVerse(lines, pages, verse, lineInVerse);
  } else {
    page = 0;
  }
  bookSel = book;
  chapterSel = chapter;
  screen = Screen::Reading;
  positionDirty = true;
  savePosition();
}

// Across book boundaries; stops at Genesis 1 and at the last chapter.
void BibleActivity::moveChapter(const int delta, const bool lastPage) {
  const auto& books = file.books();
  if (book < 0 || books.empty()) return;
  const int total = books.back().firstChapter + books.back().chapters;
  const int target = books[book].firstChapter + chapter + delta;
  if (target < 0 || target >= total) return;
  int b = 0;
  while (b + 1 < static_cast<int>(books.size()) && books[b + 1].firstChapter <= target) ++b;
  RenderLock lock(*this);  // openChapter rebuilds the text and lines render() reads
  openChapter(b, target - books[b].firstChapter, lastPage ? -1 : 1);
}

void BibleActivity::savePosition() {
  if (!positionDirty || book < 0 || readError) return;
  const auto& books = file.books();
  Bible::Position pos;
  pos.book = static_cast<uint16_t>(book);
  pos.chapter = static_cast<uint16_t>(chapter);
  const int top = pages.empty() ? 0 : pages[std::min<size_t>(page, pages.size() - 1)];
  pos.verse = static_cast<uint16_t>(BibleLayout::verseAtLine(lines, top));
  pos.line = static_cast<uint16_t>(std::max(0, top - BibleLayout::firstLineOfVerse(lines, pos.verse)));
  pos.where = books[book].name + " " + std::to_string(chapter + 1);
  if (Bible::savePosition(file.label(), pos)) positionDirty = false;
}

// Release moves one item; holding skips JUMP items per repeat, stopping at the ends.
void BibleActivity::stepList(int& index, const int count) {
  if (count <= 0) return;
  buttonNavigator.onNextRelease([&] {
    index = ButtonNavigator::nextIndex(index, count);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([&] {
    index = ButtonNavigator::previousIndex(index, count);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([&] {
    index = std::min(count - 1, index + JUMP);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([&] {
    index = std::max(0, index - JUMP);
    requestUpdate();
  });
}

void BibleActivity::loop() {
  if (waitRelease) {
    if (mappedInput.isPressed(Button::NavNext) || mappedInput.isPressed(Button::NavPrevious) ||
        mappedInput.isPressed(Button::Confirm) || mappedInput.isPressed(Button::Back)) {
      return;
    }
    waitRelease = false;
    return;
  }
  const auto& books = file.books();
  switch (screen) {
    case Screen::Versions:
      stepList(versionSel, static_cast<int>(versions.size()));
      if (mappedInput.wasReleased(Button::Back)) {
        onGoHome();
      } else if (mappedInput.wasReleased(Button::Confirm) && !versions.empty()) {
        RenderLock lock(*this);  // the book table and the chapter render() reads change
        if (!openVersion(versionSel)) return;
        Bible::Position pos;
        const auto& opened = file.books();
        if (Bible::loadPosition(file.label(), pos) && pos.book < opened.size() &&
            pos.chapter < opened[pos.book].chapters) {
          openChapter(pos.book, pos.chapter, pos.verse, pos.line);
        } else {
          screen = Screen::Books;
        }
        requestUpdate();
      }
      return;
    case Screen::Books:
      stepList(bookSel, static_cast<int>(books.size()));
      if (mappedInput.wasReleased(Button::Back)) {
        RenderLock lock(*this);
        savePosition();
        loadVersions();  // shows where reading stopped
        screen = Screen::Versions;
        requestUpdate();
      } else if (mappedInput.wasReleased(Button::Confirm) && !books.empty()) {
        chapterSel = bookSel == book ? chapter : 0;
        screen = Screen::Chapters;
        requestUpdate();
      }
      return;
    case Screen::Chapters:
      if (books.empty()) return;
      stepList(chapterSel, books[bookSel].chapters);
      if (mappedInput.wasReleased(Button::Back)) {
        screen = Screen::Books;
        requestUpdate();
      } else if (mappedInput.wasReleased(Button::Confirm)) {
        RenderLock lock(*this);
        openChapter(bookSel, chapterSel, 1);
        requestUpdate();
      }
      return;
    case Screen::Reading:
      loopReading();
      return;
  }
}

// Release turns the page (into the next/previous chapter at its ends); holding
// moves a whole chapter per repeat.
void BibleActivity::loopReading() {
  if (mappedInput.wasReleased(Button::Back)) {
    savePosition();
    chapterSel = chapter;
    bookSel = book;
    screen = Screen::Chapters;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    savePosition();
    bookSel = book;
    screen = Screen::Books;
    requestUpdate();
    return;
  }
  buttonNavigator.onNextRelease([this] {
    if (page + 1 < static_cast<int>(pages.size())) {
      ++page;
      positionDirty = true;
    } else {
      moveChapter(1, false);
    }
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    if (page > 0) {
      --page;
      positionDirty = true;
    } else {
      moveChapter(-1, true);
    }
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this] {
    moveChapter(1, false);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this] {
    moveChapter(-1, false);  // the start of the previous chapter
    requestUpdate();
  });
}

void BibleActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (screen) {
    case Screen::Versions:
      renderVersions();
      break;
    case Screen::Books:
      renderBooks();
      break;
    case Screen::Chapters:
      renderChapters();
      break;
    case Screen::Reading:
      renderReading();
      return;  // displays with the reader's refresh cycle
  }
  renderer.displayBuffer();
}

void BibleActivity::renderVersions() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_ZZ_BIBLE));
  const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
  if (versions.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, top + 10, tr(STR_ZZ_BIBLE_NONE), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + 10 + renderer.getLineHeight(UI_12_FONT_ID) + 6,
                              tr(STR_ZZ_BIBLE_NONE_HINT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  std::vector<FlashcardUi::Row> rows(versions.size());
  for (size_t i = 0; i < versions.size(); ++i) {
    rows[i].title = versionTitles[i].c_str();
    rows[i].subtitle = versionSubtitles[i].c_str();
  }
  const int height = renderer.getScreenHeight() - top - m.buttonHintsHeight - m.verticalSpacing;
  FlashcardUi::drawRows(renderer, Rect{0, top, w, height}, rows.data(), static_cast<int>(rows.size()), versionSel);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BibleActivity::renderBooks() {
  const auto& m = UITheme::getInstance().getMetrics();
  const auto& books = file.books();
  const int w = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_ZZ_BIBLE_BOOKS), file.label().c_str());
  const bool headings = books.size() == FULL_CANON_BOOKS;
  std::vector<FlashcardUi::Row> rows;
  rows.reserve(books.size() + 2);
  int selectedRow = 0;
  for (size_t i = 0; i < books.size(); ++i) {
    if (headings && (i == 0 || i == OLD_TESTAMENT_BOOKS)) {
      FlashcardUi::Row h;
      h.title = i == 0 ? tr(STR_ZZ_BIBLE_OLD_TESTAMENT) : tr(STR_ZZ_BIBLE_NEW_TESTAMENT);
      h.header = true;
      rows.push_back(h);
    }
    if (static_cast<int>(i) == bookSel) selectedRow = static_cast<int>(rows.size());
    FlashcardUi::Row r;
    r.title = books[i].name.c_str();
    rows.push_back(r);
  }
  const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int tipH = renderer.getLineHeight(SMALL_FONT_ID) + m.verticalSpacing;
  const int height = renderer.getScreenHeight() - top - m.buttonHintsHeight - m.verticalSpacing - tipH;
  FlashcardUi::drawRows(renderer, Rect{0, top, w, height}, rows.data(), static_cast<int>(rows.size()), selectedRow);
  BibleView::drawNote(renderer, top + height + m.verticalSpacing / 2, tr(STR_ZZ_BIBLE_HOLD_BOOKS));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BibleActivity::renderChapters() {
  const auto& m = UITheme::getInstance().getMetrics();
  const auto& books = file.books();
  if (books.empty()) return;
  const auto& b = books[bookSel];
  const int w = renderer.getScreenWidth();
  char count[32];
  snprintf(count, sizeof(count), "%u %s", static_cast<unsigned>(b.chapters),
           b.chapters == 1 ? tr(STR_ZZ_BIBLE_CHAPTER) : tr(STR_ZZ_BIBLE_CHAPTERS));
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, b.name.c_str(), count);
  const int top = m.topPadding + m.headerHeight;
  const int tipH = renderer.getLineHeight(SMALL_FONT_ID) + m.verticalSpacing;
  const int height = renderer.getScreenHeight() - top - m.buttonHintsHeight - m.verticalSpacing - tipH;
  const Rect area{m.contentSidePadding, top, w - 2 * m.contentSidePadding, height};
  const int perPage = BibleView::gridCellsPerPage(renderer, area);
  char label[48] = "";
  if (b.chapters > perPage) {
    const int first = chapterSel / perPage * perPage;
    const int last = std::min<int>(b.chapters, first + perPage);
    char pageOf[32];
    snprintf(pageOf, sizeof(pageOf), tr(STR_ZZ_BIBLE_PAGE_OF), chapterSel / perPage + 1,
             (b.chapters + perPage - 1) / perPage);
    snprintf(label, sizeof(label), "%d\xE2\x80\x93%d  \xC2\xB7  %s", first + 1, last, pageOf);
  }
  BibleView::drawChapterGrid(renderer, area, b.chapters, chapterSel, label);
  if (b.chapters > JUMP)
    BibleView::drawNote(renderer, top + height + m.verticalSpacing / 2, tr(STR_ZZ_BIBLE_HOLD_CHAPTERS));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_ZZ_BIBLE_READ), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BibleActivity::renderReading() {
  const auto& m = UITheme::getInstance().getMetrics();
  const auto& books = file.books();
  const int w = renderer.getScreenWidth();
  char title[48];
  snprintf(title, sizeof(title), "%s %d", books[book].name.c_str(), chapter + 1);
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, title, file.label().c_str());
  const int top = pageTop();
  const int footerY = top + pageHeight() + m.verticalSpacing / 2;
  if (readError) {
    renderer.drawCenteredText(UI_12_FONT_ID, top + 10, tr(STR_ZZ_BIBLE_READ_ERROR), true, EpdFontFamily::BOLD);
  } else {
    const int first = pages[page];
    const int last = page + 1 < static_cast<int>(pages.size()) ? pages[page + 1] : static_cast<int>(lines.size());
    BibleView::drawPage(renderer, text, lines, first, last, top, geometry(), VERSE_GAP);
    char pageOf[32];
    snprintf(pageOf, sizeof(pageOf), tr(STR_ZZ_BIBLE_PAGE_OF), page + 1, static_cast<int>(pages.size()));
    char footer[96];
    snprintf(footer, sizeof(footer), "%s  \xC2\xB7  %s %d:%d\xE2\x80\x93%d  \xC2\xB7  %s", file.label().c_str(),
             books[book].abbrev.c_str(), chapter + 1, BibleLayout::verseAtLine(lines, first),
             BibleLayout::verseAtLine(lines, last - 1), pageOf);
    BibleView::drawNote(renderer, footerY, footer);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_ZZ_BIBLE_BOOKS), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
}
