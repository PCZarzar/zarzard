#include "FlashcardDeckListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "FlashcardLinearProgress.h"
#include "FlashcardModeSelectActivity.h"
#include "FlashcardSettingsActivity.h"
#include "FlashcardSrs.h"
#include "FlashcardStore.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int EXTRA_ITEMS = 1;  // Settings, after the decks
}  // namespace

void FlashcardDeckListActivity::onEnter() {
  Activity::onEnter();
  FLASHCARD_STORE.loadFromFile();  // missing file on first use: defaults stay
  today = FlashcardSrs::getToday();
  loadDeckList();
  requestUpdate();
}

void FlashcardDeckListActivity::loadDeckList() {
  decks.clear();
  totalDueToday = 0;
  failedDecks = 0;

  uint32_t newTotal = 0;
  const auto paths = FlashcardDeck::listDecks();
  decks.reserve(paths.size());
  for (const auto& path : paths) {
    FlashcardDeck deck;
    if (!deck.loadFromCsv(path.c_str())) {
      ++failedDecks;
      continue;
    }
    DeckEntry entry;
    entry.name = deck.getName();
    entry.path = path;
    entry.stats = deck.getStats(today);
    entry.truncated = deck.isTruncated();
    totalDueToday += entry.stats.dueCards;
    newTotal += entry.stats.newCards;
    decks.push_back(std::move(entry));
  }
  // New cards count only up to what today's quota still allows.
  uint16_t count = FLASHCARD_STORE.newTodayCount;  // copies: read-only peek
  uint32_t date = FLASHCARD_STORE.newTodayDate;
  const uint16_t quota = FlashcardDeck::newCardsAvailable(FLASHCARD_STORE.newPerDay, count, date, today);
  totalDueToday += std::min<uint32_t>(newTotal, quota);

  const int total = static_cast<int>(decks.size()) + EXTRA_ITEMS;
  if (selectedIndex >= total) selectedIndex = total - 1;
}

int FlashcardDeckListActivity::findDeck(const std::string& path) const {
  for (size_t i = 0; i < decks.size(); ++i) {
    if (decks[i].path == path) return static_cast<int>(i);
  }
  return -1;
}

FlashcardNextDeck FlashcardDeckListActivity::resolveNextDeckThunk(void* self) {
  return static_cast<FlashcardDeckListActivity*>(self)->resolveNextDeck();
}

// Called by the review when it ends. decks[] stats are from before the
// session, which only matters for the current deck — and that one is visited.
FlashcardNextDeck FlashcardDeckListActivity::resolveNextDeck() {
  offeredNextPath.clear();
  const int current = findDeck(currentPath);
  int next = -1;
  FlashcardNextDeck offer;

  if (roundMode == FlashcardReviewActivity::ReviewMode::Linear) {
    // Next deck in list order, without wrapping.
    if (current >= 0 && current + 1 < static_cast<int>(decks.size())) next = current + 1;
  } else {
    const uint32_t now = FlashcardSrs::getToday();
    if (now == 0) return offer;                      // no trustworthy date: no spaced round
    uint16_t count = FLASHCARD_STORE.newTodayCount;  // copies: read-only peek
    uint32_t date = FLASHCARD_STORE.newTodayDate;
    const uint16_t quota = FlashcardDeck::newCardsAvailable(FLASHCARD_STORE.newPerDay, count, date, now);
    std::vector<DeckStats> stats;
    std::vector<bool> visited;
    stats.reserve(decks.size());
    visited.reserve(decks.size());
    for (const auto& d : decks) {
      stats.push_back(d.stats);
      // Decks over the card limit are Linear only: skip them like visited ones.
      visited.push_back(d.truncated ||
                        std::find(visitedPaths.begin(), visitedPaths.end(), d.path) != visitedPaths.end());
    }
    next = FlashcardDeck::nextSpacedDeck(stats, visited, current, quota);
    if (next >= 0) {
      offer.cards = FlashcardDeck::spacedSessionSize(decks[next].stats, quota, FLASHCARD_STORE.maxReviewPerDay);
    }
  }

  if (next >= 0) {
    offer.name = decks[next].name;
    offeredNextPath = decks[next].path;
  }
  return offer;
}

void FlashcardDeckListActivity::startReview(const int deckIndex, const FlashcardReviewActivity::ReviewMode mode,
                                            const bool resumeLinear) {
  roundMode = mode;
  currentPath = decks[deckIndex].path;
  visitedPaths.push_back(currentPath);
  offeredNextPath.clear();

  startActivityForResult(
      std::make_unique<FlashcardReviewActivity>(renderer, mappedInput, currentPath, mode, FLASHCARD_STORE.shuffle != 0,
                                                resumeLinear, FlashcardNextDeckProvider{this, &resolveNextDeckThunk}),
      [this, mode](const ActivityResult& result) {
        today = FlashcardSrs::getToday();
        loadDeckList();
        const auto* menu = std::get_if<MenuResult>(&result.data);
        const int next = offeredNextPath.empty() ? -1 : findDeck(offeredNextPath);
        if (menu && menu->action == FlashcardReviewActivity::RESULT_NEXT_DECK && next >= 0) {
          selectedIndex = next;
          // Chained linear decks pick up their own saved pass, if any.
          FlashcardLinearProgress::Saved saved;
          const bool resume = mode == FlashcardReviewActivity::ReviewMode::Linear &&
                              FlashcardLinearProgress::load(decks[next].path, saved);
          startReview(next, mode, resume);
          return;
        }
        visitedPaths.clear();  // the round ends when we are back on the list
        requestUpdate();
      });
}

void FlashcardDeckListActivity::loop() {
  const int total = static_cast<int>(decks.size()) + EXTRA_ITEMS;

  buttonNavigator.onNext([this, total] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, total);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, total] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, total);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::FLASHCARDS);
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;

  const int deckCount = static_cast<int>(decks.size());
  if (selectedIndex < deckCount) {
    const int deckIndex = selectedIndex;
    startActivityForResult(
        std::make_unique<FlashcardModeSelectActivity>(renderer, mappedInput, decks[deckIndex].name,
                                                      decks[deckIndex].path, decks[deckIndex].truncated),
        [this, deckIndex](const ActivityResult& result) {
          const auto* menu = std::get_if<MenuResult>(&result.data);
          if (result.isCancelled || !menu || deckIndex >= static_cast<int>(decks.size())) return;
          const bool resume = menu->action == FlashcardModeSelectActivity::ACTION_LINEAR_CONTINUE;
          const auto mode = resume || menu->action == static_cast<int>(FlashcardReviewActivity::ReviewMode::Linear)
                                ? FlashcardReviewActivity::ReviewMode::Linear
                                : FlashcardReviewActivity::ReviewMode::Spaced;
          visitedPaths.clear();
          startReview(deckIndex, mode, resume);
        });
  } else {
    startActivityForResult(std::make_unique<FlashcardSettingsActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             today = FlashcardSrs::getToday();
                             loadDeckList();
                             requestUpdate();
                           });
  }
}

void FlashcardDeckListActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePad = metrics.contentSidePadding;
  const int cardWidth = pageWidth - sidePad * 2;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLASHCARDS));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  char subtitle[64];
  snprintf(subtitle, sizeof(subtitle), "%s %u", tr(STR_FLASHCARD_REVIEW_TODAY), static_cast<unsigned>(totalDueToday));
  renderer.drawText(SMALL_FONT_ID, sidePad, y, subtitle);
  y += renderer.getLineHeight(SMALL_FONT_ID);
  if (failedDecks > 0) {
    snprintf(subtitle, sizeof(subtitle), "%s %u", tr(STR_FLASHCARD_LOAD_FAILED), static_cast<unsigned>(failedDecks));
    renderer.drawText(SMALL_FONT_ID, sidePad, y, subtitle);
    y += renderer.getLineHeight(SMALL_FONT_ID);
  }
  y += metrics.verticalSpacing;

  const int deckCount = static_cast<int>(decks.size());
  const int total = deckCount + EXTRA_ITEMS;
  const int line12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineSmall = renderer.getLineHeight(SMALL_FONT_ID);
  const int rowH = 8 + line12 + 4 + lineSmall + 8;
  constexpr int rowGap = 6;
  const int listBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (deckCount == 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, y + 10, tr(STR_FLASHCARD_NO_DECKS), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, y + 10 + line12 + 6, tr(STR_FLASHCARD_IMPORT_HINT));
    y += 10 + line12 + 6 + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing * 2;
  }

  const int visible = std::max(1, (listBottom - y + rowGap) / (rowH + rowGap));
  const int first = std::clamp(selectedIndex - visible + 1, 0, std::max(0, total - visible));

  for (int i = first; i < total && i < first + visible; ++i) {
    const bool inv = (i == selectedIndex);
    if (inv) renderer.fillRect(sidePad, y, cardWidth, rowH, true);
    renderer.drawRect(sidePad, y, cardWidth, rowH);

    if (i < deckCount) {
      const auto& d = decks[i];
      const std::string name =
          renderer.truncatedText(UI_12_FONT_ID, d.name.c_str(), cardWidth - 16, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, sidePad + 8, y + 8, name.c_str(), !inv, EpdFontFamily::BOLD);
      char stats[96];
      snprintf(stats, sizeof(stats), "%s %u   %s %u   %s %u%s", tr(STR_FLASHCARD_NEW),
               static_cast<unsigned>(d.stats.newCards), tr(STR_FLASHCARD_DUE), static_cast<unsigned>(d.stats.dueCards),
               tr(STR_FLASHCARD_TOTAL), static_cast<unsigned>(d.stats.totalCards), d.truncated ? "+" : "");
      renderer.drawText(SMALL_FONT_ID, sidePad + 8, y + 8 + line12 + 4, stats, !inv);
    } else {
      renderer.drawText(UI_12_FONT_ID, sidePad + 8, y + (rowH - line12) / 2, tr(STR_FLASHCARD_SETTINGS), !inv);
    }
    y += rowH + rowGap;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
