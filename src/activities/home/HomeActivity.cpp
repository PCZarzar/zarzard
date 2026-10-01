#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/flashcard/FlashcardDeckOptions.h"
#include "activities/flashcard/FlashcardHistory.h"
#include "activities/flashcard/FlashcardHomeActivity.h"
#include "activities/flashcard/FlashcardPlanRefresh.h"
#include "activities/flashcard/FlashcardSrs.h"
#include "activities/flashcard/FlashcardStore.h"
#include "components/UITheme.h"
#include "components/themes/zarzard/ZarzardHome.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int count = 6;  // File Browser, Recents, File transfer, Flashcards, Summaries, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  zarzard = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::ZARZARD;
  if (zarzard) {
    // Read the flashcard stores before Home uses them: counting today's cards
    // saves them, and saving the never-loaded defaults wiped the user's
    // settings, the last known day and every deck's options.
    FLASHCARD_STORE.loadFromFile();
    FLASHCARD_DECK_OPTIONS.loadFromFile();
    zzBuildItems();
    selectorIndex = 0;
    for (size_t i = 0; i < zzItems.size(); ++i) {
      if (initialMenuItem != HomeMenuItem::NONE && zzItems[i] == initialMenuItem) selectorIndex = static_cast<int>(i);
    }
    zzLoadStudy();
    requestUpdate();
    return;
  }

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  if (zarzard) {
    zzLoop();
    return;
  }
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::FLASHCARDS:
        onFlashcardsOpen();
        break;
      case HomeMenuItem::SUMMARIES:
        onSummariesOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }

  const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
  const int recentCount = std::min(static_cast<int>(recentBooks.size()), coverColumnCount);
  const int coverColumnWidth = (renderer.getScreenWidth() - 2 * metrics.contentSidePadding) / coverColumnCount;
  int touchedBook = -1;
  const auto coverTouch = mappedInput.colTouch(touchedBook, metrics.contentSidePadding, coverColumnWidth, recentCount,
                                               metrics.homeTopPadding,
                                               metrics.homeTopPadding + metrics.homeCoverTileHeight, coverColumnWidth);
  if (coverTouch != MappedInputManager::RowTouch::None) {
    if (coverTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedBook) {
        selectorIndex = touchedBook;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedBook;
      activateSelection();
    }
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuSelection =
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size();
  const int renderedMenuCount =
      menuCount - (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));
  int menuRow = -1;
  // Row height from the theme, not the metrics table: RoundedRaff draws
  // font-derived rows and the touch grid must match the visuals exactly.
  const int menuRowHeight = GUI.getMenuRowHeight(renderer);
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, menuRowHeight + metrics.menuSpacing, renderedMenuCount,
                                              0, INT32_MAX, menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  if (zarzard) {
    zzRender();
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding or the band (and a
  // centered title, e.g. RoundedRaff's book title) sinks into the tile.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_FLASHCARDS),   tr(STR_SUMMARIES),         tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Bookmark, Book, Settings};

  if (hasOpdsServers) {
    menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
    menuIcons.insert(menuIcons.begin() + 2, Library);
  }

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    // Insert Continue Reading at the top if enabled in theme
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
           pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                         metrics.homeMenuTopOffset + metrics.buttonHintsHeight)},
      static_cast<int>(menuItems.size()),
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onSummariesOpen() { activityManager.goToSummaries(); }

void HomeActivity::onFlashcardsOpen() {
  activityManager.replaceActivity(std::make_unique<FlashcardHomeActivity>(renderer, mappedInput));
}

// ─── Zarzard layout ──────────────────────────────────────────────────────────

void HomeActivity::zzBuildItems() {
  zzItems.clear();
  zzItems.reserve(8);
  zzItems.push_back(HomeMenuItem::FLASHCARDS);
  if (!recentBooks.empty()) zzItems.push_back(HomeMenuItem::NONE);  // continue reading
  zzItems.push_back(HomeMenuItem::SUMMARIES);
  zzItems.push_back(HomeMenuItem::FILE_BROWSER);
  zzItems.push_back(HomeMenuItem::RECENTS);
  zzItems.push_back(HomeMenuItem::FILE_TRANSFER);
  if (hasOpdsServers) zzItems.push_back(HomeMenuItem::OPDS_BROWSER);
  zzItems.push_back(HomeMenuItem::SETTINGS_MENU);
}

// Today's Spaced total comes from the count the Spaced list remembers; when it
// is from another day it is counted again after the first frame (zzLoop).
void HomeActivity::zzLoadStudy() {
  zzCardsToday = -1;
  zzCountPending = false;
  zzStreak = 0;
  zzDonePercent = 0;
  const uint32_t today = FlashcardSrs::getToday();
  if (today == 0) return;
  if (FLASHCARD_STORE.homeCardsDay == today) {
    zzCardsToday = FLASHCARD_STORE.homeCardsToday;
  } else {
    zzCountPending = true;
  }
  zzStreak = FlashcardHistory::summarize(today, FLASHCARD_STORE.freeDaysMask).currentStreak;
  const auto records = FlashcardHistory::readRange(today, today);
  const uint32_t done = records.empty() ? 0 : records.front().spaced();
  const uint32_t left = zzCardsToday > 0 ? static_cast<uint32_t>(zzCardsToday) : 0;
  zzDonePercent = done + left == 0 ? 0 : static_cast<int>(done * 100 / (done + left));
}

void HomeActivity::zzActivate(const HomeMenuItem item) {
  switch (item) {
    case HomeMenuItem::NONE:
      if (!recentBooks.empty()) onSelectBook(recentBooks[0].path);
      break;
    case HomeMenuItem::FLASHCARDS:
      onFlashcardsOpen();
      break;
    case HomeMenuItem::SUMMARIES:
      onSummariesOpen();
      break;
    case HomeMenuItem::FILE_BROWSER:
      onFileBrowserOpen();
      break;
    case HomeMenuItem::RECENTS:
      onRecentsOpen();
      break;
    case HomeMenuItem::FILE_TRANSFER:
      onFileTransferOpen();
      break;
    case HomeMenuItem::OPDS_BROWSER:
      onOpdsBrowserOpen();
      break;
    case HomeMenuItem::SETTINGS_MENU:
      onSettingsOpen();
      break;
  }
}

void HomeActivity::zzLoop() {
  if (zzCountPending && firstRenderDone) {
    // Reads every deck: done here, outside the render task, then shown.
    flashcardCountCardsToday(FlashcardSrs::getToday());
    {
      RenderLock lock(*this);
      zzLoadStudy();
    }
    requestUpdate();
    return;
  }
  const int count = static_cast<int>(zzItems.size());
  buttonNavigator.onNext([this, count] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    onSelectBook(recentBooks[0].path);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && selectorIndex < count) {
    zzActivate(zzItems[selectorIndex]);
  }
}

void HomeActivity::zzRender() {
  ZarzardHomeView view;
  auto isSelected = [this](const HomeMenuItem item) {
    return selectorIndex < static_cast<int>(zzItems.size()) && zzItems[selectorIndex] == item;
  };
  view.studyTitle = tr(STR_FLASHCARDS);
  view.studySelected = isSelected(HomeMenuItem::FLASHCARDS);
  view.cardsToday = zzCardsToday;
  view.cardsLabel = tr(STR_FLASHCARD_CARDS);
  view.dueLabel = tr(STR_ZZ_DUE_TODAY);
  view.allDoneLabel = tr(STR_ZZ_ALL_DONE);
  view.dateUnknownLabel = tr(STR_ZZ_DATE_UNKNOWN);
  view.streakText = std::to_string(zzStreak) + " " + tr(STR_ZZ_STREAK_DAYS);
  view.donePercent = zzDonePercent;
  if (!recentBooks.empty()) {
    const RecentBook& book = recentBooks[0];
    view.hasBook = true;
    view.bookSelected = isSelected(HomeMenuItem::NONE);
    view.continueLabel = tr(STR_CONTINUE_READING);
    view.bookTitle = book.title.empty() ? book.path.substr(book.path.rfind('/') + 1) : book.title;
    view.bookAuthor = book.author;
  }
  view.tiles.reserve(zzItems.size());
  for (const HomeMenuItem item : zzItems) {
    ZarzardHomeTile tile{nullptr, nullptr, isSelected(item)};
    switch (item) {
      case HomeMenuItem::SUMMARIES:
        tile.icon = &ZarzardIcons::Notebook40;
        tile.label = tr(STR_SUMMARIES);
        break;
      case HomeMenuItem::FILE_BROWSER:
        tile.icon = &ZarzardIcons::FolderOpen40;
        tile.label = tr(STR_BROWSE_FILES);
        break;
      case HomeMenuItem::RECENTS:
        tile.icon = &ZarzardIcons::ClockCounterClockwise40;
        tile.label = tr(STR_MENU_RECENT_BOOKS);
        break;
      case HomeMenuItem::FILE_TRANSFER:
        tile.icon = &ZarzardIcons::ArrowsLeftRight40;
        tile.label = tr(STR_FILE_TRANSFER);
        break;
      case HomeMenuItem::OPDS_BROWSER:
        tile.icon = &ZarzardIcons::Rss40;
        tile.label = tr(STR_OPDS_BROWSER);
        break;
      case HomeMenuItem::SETTINGS_MENU:
        tile.icon = &ZarzardIcons::GearSix40;
        tile.label = tr(STR_SETTINGS_TITLE);
        break;
      default:
        continue;  // the study and continue cards are drawn apart
    }
    view.tiles.push_back(tile);
  }
  view.batteryPercent = powerManager.getBatteryPercentage();
  view.showBatteryPercent = SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;

  renderer.clearScreen();
  ZarzardUi::drawHome(renderer, view);
  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstRenderDone = true;
}
