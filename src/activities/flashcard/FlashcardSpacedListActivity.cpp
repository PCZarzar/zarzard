#include "FlashcardSpacedListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "FlashcardClockSync.h"
#include "FlashcardDateActivity.h"
#include "FlashcardPlanRefresh.h"
#include "FlashcardSrs.h"
#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FlashcardSpacedListActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

void FlashcardSpacedListActivity::reload() {
  today = FlashcardSrs::getToday();
  clockState = FlashcardClockSync::state();

  decks.clear();
  failedDecks = 0;
  standbyDecks = 0;
  const auto paths = FlashcardCatalog::allDecks();
  std::vector<FlashcardCatalog::Entry> all;
  all.reserve(paths.size());
  for (const auto& path : paths) {
    FlashcardCatalog::Entry e;
    if (!FlashcardCatalog::load(path, today, e)) {
      ++failedDecks;
      continue;
    }
    all.push_back(std::move(e));
  }
  // Every deck takes part in the split, so shares stay stable whatever is listed.
  flashcardRefreshPlan(all, today);

  decks.reserve(all.size());
  uint32_t due = 0;
  uint32_t fresh = 0;
  for (auto& e : all) {
    if (e.state == FlashcardDeckState::StandBy) {
      ++standbyDecks;
      continue;
    }
    if (!e.truncated) {
      due += e.stats.dueCards;
      fresh += std::min<uint32_t>(e.stats.newCards, e.newToday);
    }
    decks.push_back(std::move(e));
  }
  cardsToday = due + fresh;
  flashcardRememberCardsToday(today, cardsToday);
  FlashcardCatalog::sortForSpaced(decks);
  buildItems();
}

void FlashcardSpacedListActivity::buildItems() {
  items.clear();
  items.reserve(decks.size() + 3);
  const bool clockOk = clockState != FlashcardClock::State::Unset;
  if (clockOk && firstPendingUnvisited() >= 0) items.push_back({ItemKind::ReviewAll});
  if (clockState != FlashcardClock::State::Ok) items.push_back({ItemKind::SyncDate});
  bool headerAdded = false;
  for (int i = 0; i < static_cast<int>(decks.size()); ++i) {
    if (!headerAdded && !FlashcardCatalog::isPending(decks[i])) {
      items.push_back({ItemKind::UpToDateHeader});
      headerAdded = true;
    }
    items.push_back({ItemKind::Deck, i});
  }
  if (selectedIndex >= static_cast<int>(items.size())) selectedIndex = 0;
  if (!items.empty() && items[selectedIndex].kind == ItemKind::UpToDateHeader) {
    selectedIndex = std::min(selectedIndex + 1, static_cast<int>(items.size()) - 1);
  }
}

int FlashcardSpacedListActivity::findDeck(const std::string& path) const {
  for (size_t i = 0; i < decks.size(); ++i) {
    if (decks[i].path == path) return static_cast<int>(i);
  }
  return -1;
}

// Most due first: the list is sorted, so the first pending unvisited deck.
int FlashcardSpacedListActivity::firstPendingUnvisited() const {
  for (size_t i = 0; i < decks.size(); ++i) {
    if (!FlashcardCatalog::isPending(decks[i])) continue;
    if (std::find(visitedPaths.begin(), visitedPaths.end(), decks[i].path) != visitedPaths.end()) continue;
    return static_cast<int>(i);
  }
  return -1;
}

FlashcardNextDeck FlashcardSpacedListActivity::resolveNextDeckThunk(void* self) {
  return static_cast<FlashcardSpacedListActivity*>(self)->resolveNextDeck();
}

// Called by the review when it ends. The session used some of today's new
// cards, so each deck's remaining share is read again; decks[] stats are from
// before the session, which only matters for the current deck — and that one
// is visited.
FlashcardNextDeck FlashcardSpacedListActivity::resolveNextDeck() {
  offeredNextPath.clear();
  FlashcardNextDeck offer;
  const uint32_t now = FlashcardSrs::getToday();
  if (now == 0) return offer;
  for (auto& d : decks) d.newToday = FLASHCARD_DECK_OPTIONS.newRemaining(d.path, now);
  const int next = firstPendingUnvisited();
  if (next < 0) return offer;
  offer.name = decks[next].name;
  offer.cards =
      FlashcardDeck::spacedSessionSize(decks[next].stats, decks[next].newToday, FLASHCARD_STORE.maxReviewPerDay);
  offeredNextPath = decks[next].path;
  return offer;
}

void FlashcardSpacedListActivity::startReview(const int deckIndex) {
  const std::string path = decks[deckIndex].path;
  visitedPaths.push_back(path);
  offeredNextPath.clear();

  startActivityForResult(std::make_unique<FlashcardReviewActivity>(
                             renderer, mappedInput, path, FlashcardReviewActivity::ReviewMode::Spaced, false, false,
                             FlashcardNextDeckProvider{this, &resolveNextDeckThunk}),
                         [this](const ActivityResult& result) {
                           // Result handlers run unlocked; reload() rebuilds what render() reads.
                           RenderLock lock(*this);
                           const std::string nextPath = offeredNextPath;
                           reload();
                           const auto* menu = std::get_if<MenuResult>(&result.data);
                           const int next = nextPath.empty() ? -1 : findDeck(nextPath);
                           if (menu && menu->action == FlashcardReviewActivity::RESULT_NEXT_DECK && next >= 0) {
                             startReview(next);
                             return;
                           }
                           visitedPaths.clear();  // the round ends when we are back on the list
                           buildItems();
                           selectedIndex = 0;
                           requestUpdate();
                         });
}

void FlashcardSpacedListActivity::openSync() {
  startActivityForResult(std::make_unique<FlashcardDateActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    RenderLock lock(*this);
    reload();
    requestUpdate();
  });
}

// Input changes the state render() reads (queues, lists, card text), so it
// runs under the render lock; the render task waits for it, never the reverse.
void FlashcardSpacedListActivity::loop() {
  RenderLock lock(*this);
  loopInput();
}

void FlashcardSpacedListActivity::loopInput() {
  if (items.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
    return;
  }
  // Header rows are skipped by the cursor.
  const auto isHeader = [this](int i) { return items[i].kind == ItemKind::UpToDateHeader; };
  const int count = static_cast<int>(items.size());
  buttonNavigator.onNext([this, count, &isHeader] {
    int i = selectedIndex;
    do i = ButtonNavigator::nextIndex(i, count);
    while (isHeader(i) && i != selectedIndex);
    selectedIndex = i;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count, &isHeader] {
    int i = selectedIndex;
    do i = ButtonNavigator::previousIndex(i, count);
    while (isHeader(i) && i != selectedIndex);
    selectedIndex = i;
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;

  const Item item = items[selectedIndex];
  switch (item.kind) {
    case ItemKind::ReviewAll: {
      visitedPaths.clear();
      const int first = firstPendingUnvisited();
      if (first >= 0) startReview(first);
      break;
    }
    case ItemKind::SyncDate:
      openSync();
      break;
    case ItemKind::Deck: {
      if (clockState == FlashcardClock::State::Unset) {
        // Never rate cards against an unknown date: jump to the sync row.
        for (int i = 0; i < count; ++i) {
          if (items[i].kind == ItemKind::SyncDate) selectedIndex = i;
        }
        requestUpdate();
        break;
      }
      if (decks[item.deck].truncated) break;  // Linear only: ratings could not be saved
      visitedPaths.clear();
      startReview(item.deck);
      break;
    }
    case ItemKind::UpToDateHeader:
      break;
  }
}

void FlashcardSpacedListActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePad = metrics.contentSidePadding;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLASHCARD_SPACED));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  char line[96];
  snprintf(line, sizeof(line), "%s %u", tr(STR_FLASHCARD_REVIEW_TODAY), static_cast<unsigned>(cardsToday));
  renderer.drawText(SMALL_FONT_ID, sidePad, y, line);
  y += renderer.getLineHeight(SMALL_FONT_ID);
  if (failedDecks > 0) {
    snprintf(line, sizeof(line), "%s %u", tr(STR_FLASHCARD_LOAD_FAILED), static_cast<unsigned>(failedDecks));
    renderer.drawText(SMALL_FONT_ID, sidePad, y, line);
    y += renderer.getLineHeight(SMALL_FONT_ID);
  }
  if (standbyDecks > 0) {
    snprintf(line, sizeof(line), "%u %s %s", static_cast<unsigned>(standbyDecks), tr(STR_FLASHCARD_DECKS),
             tr(STR_FLASHCARD_IN_STANDBY));
    renderer.drawText(SMALL_FONT_ID, sidePad, y, line);
    y += renderer.getLineHeight(SMALL_FONT_ID);
  }
  y += metrics.verticalSpacing / 2;

  const int listH = pageHeight - y - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (decks.empty()) {
    const bool allStandby = standbyDecks > 0;
    renderer.drawCenteredText(UI_12_FONT_ID, y + 10,
                              allStandby ? tr(STR_FLASHCARD_NO_ACTIVE) : tr(STR_FLASHCARD_NO_DECKS), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, y + 10 + renderer.getLineHeight(UI_12_FONT_ID) + 6,
                              allStandby ? tr(STR_FLASHCARD_NO_ACTIVE_HINT) : tr(STR_FLASHCARD_IMPORT_HINT));
  }

  // Per-row text lives here while drawRows runs.
  std::vector<FlashcardUi::Row> rows(items.size());
  std::vector<std::string> subs(items.size());
  std::vector<std::string> values(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    auto& row = rows[i];
    switch (items[i].kind) {
      case ItemKind::ReviewAll:
        row.title = tr(STR_FLASHCARD_REVIEW_ALL);
        values[i] = std::to_string(cardsToday);
        row.value = values[i].c_str();
        break;
      case ItemKind::SyncDate:
        row.title = tr(STR_FLASHCARD_SYNC_DATE);
        row.subtitle = clockState == FlashcardClock::State::Unset ? tr(STR_FLASHCARD_CLOCK_NOT_SET)
                                                                  : tr(STR_FLASHCARD_SYNC_RECOMMENDED);
        break;
      case ItemKind::UpToDateHeader:
        row.title = tr(STR_FLASHCARD_UP_TO_DATE);
        row.header = true;
        break;
      case ItemKind::Deck: {
        const auto& d = decks[items[i].deck];
        row.title = d.name.c_str();
        char buf[128];
        if (d.truncated) {
          snprintf(buf, sizeof(buf), "%s%s%s", d.folder.c_str(), d.folder.empty() ? "" : " \xC2\xB7 ",
                   tr(STR_FLASHCARD_TOO_MANY_CARDS));
        } else {
          // Due · new today (of the deck's new cards) · total
          snprintf(buf, sizeof(buf), "%s%s%s %u \xC2\xB7 %s %u/%u \xC2\xB7 %s %u", d.folder.c_str(),
                   d.folder.empty() ? "" : " \xC2\xB7 ", tr(STR_FLASHCARD_DUE), static_cast<unsigned>(d.stats.dueCards),
                   tr(STR_FLASHCARD_NEW), static_cast<unsigned>(std::min(d.newToday, d.stats.newCards)),
                   static_cast<unsigned>(d.stats.newCards), tr(STR_FLASHCARD_TOTAL),
                   static_cast<unsigned>(d.stats.totalCards));
        }
        subs[i] = buf;
        row.subtitle = subs[i].c_str();
        if (FlashcardCatalog::isPending(d)) {
          values[i] =
              std::to_string(FlashcardDeck::spacedSessionSize(d.stats, d.newToday, FLASHCARD_STORE.maxReviewPerDay));
          row.value = values[i].c_str();
        }
        break;
      }
    }
  }
  FlashcardUi::drawRows(renderer, Rect{0, y, pageWidth, listH}, rows.data(), static_cast<int>(rows.size()),
                        selectedIndex);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
