#include "FlashcardDeckOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "FlashcardDeckOptions.h"
#include "FlashcardPlanRefresh.h"
#include "FlashcardSrs.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* stateName(const FlashcardDeckState s) {
  switch (s) {
    case FlashcardDeckState::Active:
      return tr(STR_FLASHCARD_ACTIVE);
    case FlashcardDeckState::ReviewsOnly:
      return tr(STR_FLASHCARD_REVIEWS_ONLY);
    default:
      return tr(STR_FLASHCARD_STANDBY);
  }
}

const char* stateDesc(const FlashcardDeckState s) {
  switch (s) {
    case FlashcardDeckState::Active:
      return tr(STR_FLASHCARD_ACTIVE_DESC);
    case FlashcardDeckState::ReviewsOnly:
      return tr(STR_FLASHCARD_REVIEWS_ONLY_DESC);
    default:
      return tr(STR_FLASHCARD_STANDBY_DESC);
  }
}
}  // namespace

void FlashcardDeckOptionsActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

void FlashcardDeckOptionsActivity::onExit() {
  if (dirty) {
    FLASHCARD_DECK_OPTIONS.prune(FlashcardCatalog::allDecks());
    FLASHCARD_DECK_OPTIONS.saveToFile();
  }
  Activity::onExit();
}

void FlashcardDeckOptionsActivity::reload() {
  today = FlashcardSrs::getToday();
  entries.clear();
  const auto paths = FlashcardCatalog::allDecks();
  entries.reserve(paths.size());
  for (const auto& path : paths) {
    FlashcardCatalog::Entry e;
    if (FlashcardCatalog::load(path, today, e)) entries.push_back(std::move(e));
  }
  replan();

  items.clear();
  items.reserve(entries.size() + 8);
  std::string folder;
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    const auto& e = entries[i];
    if (!e.folder.empty() && e.folder != folder) {
      folder = e.folder;
      Item f;
      f.isFolder = true;
      f.folder = folder;
      items.push_back(f);
    }
    Item d;
    d.folder = e.folder;
    d.entry = i;
    items.push_back(d);
  }
  if (selectedIndex >= static_cast<int>(items.size())) selectedIndex = 0;
}

void FlashcardDeckOptionsActivity::replan() { flashcardRefreshPlan(entries, today); }

FlashcardDeckState FlashcardDeckOptionsActivity::folderState(const std::string& folder, bool& mixed) const {
  mixed = false;
  bool any = false;
  FlashcardDeckState state = FlashcardDeckState::StandBy;
  for (const auto& e : entries) {
    if (e.folder != folder || e.truncated) continue;
    if (any && e.state != state) mixed = true;
    state = e.state;
    any = true;
  }
  return state;
}

void FlashcardDeckOptionsActivity::setFolderState(const std::string& folder, const FlashcardDeckState state) {
  for (const auto& e : entries) {
    if (e.folder == folder && !e.truncated) FLASHCARD_DECK_OPTIONS.set(e.path, state, e.share);
  }
  dirty = true;
  replan();
}

void FlashcardDeckOptionsActivity::loop() {
  if (deckEntry >= 0) {
    loopDeck();
    return;
  }
  const int count = static_cast<int>(items.size());
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (count == 0) return;
  buttonNavigator.onNext([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;

  const Item& it = items[selectedIndex];
  RenderLock lock(*this);
  if (it.isFolder) {
    bool mixed = false;
    const FlashcardDeckState s = folderState(it.folder, mixed);
    setFolderState(it.folder, mixed ? FlashcardDeckState::Active : FlashcardPlanner::nextState(s));
  } else if (!entries[it.entry].truncated) {
    deckEntry = it.entry;
    deckRow = 0;
    editingShare = false;
  }
  lock.unlock();
  requestUpdate();
}

void FlashcardDeckOptionsActivity::loopDeck() {
  auto& e = entries[deckEntry];
  const bool active = e.state == FlashcardDeckState::Active;
  const int rowCount = active ? 2 : 1;
  if (deckRow >= rowCount) deckRow = 0;

  if (editingShare) {
    const auto step = [this, &e](const int dir) {
      const int share = std::clamp(e.share + dir, static_cast<int>(FlashcardPlanner::SHARE_MIN),
                                   static_cast<int>(FlashcardPlanner::SHARE_MAX));
      if (share == e.share) return;
      RenderLock lock(*this);
      FLASHCARD_DECK_OPTIONS.set(e.path, e.state, static_cast<uint8_t>(share));
      dirty = true;
      replan();
      lock.unlock();
      requestUpdate();
    };
    buttonNavigator.onNext([&step] { step(+1); });
    buttonNavigator.onPrevious([&step] { step(-1); });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      editingShare = false;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    deckEntry = -1;
    requestUpdate();
    return;
  }
  buttonNavigator.onNext([this, rowCount] {
    deckRow = ButtonNavigator::nextIndex(deckRow, rowCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, rowCount] {
    deckRow = ButtonNavigator::previousIndex(deckRow, rowCount);
    requestUpdate();
  });
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;
  if (deckRow == 0) {
    RenderLock lock(*this);
    FLASHCARD_DECK_OPTIONS.set(e.path, FlashcardPlanner::nextState(e.state), e.share);
    dirty = true;
    replan();
    lock.unlock();
  } else {
    editingShare = true;
  }
  requestUpdate();
}

void FlashcardDeckOptionsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  const char* title = deckEntry >= 0 ? entries[deckEntry].name.c_str() : tr(STR_FLASHCARD_DECK_LIST);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  if (deckEntry >= 0) {
    renderDeck(top, height);
  } else if (items.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, top + 10, tr(STR_FLASHCARD_NO_DECKS), true, EpdFontFamily::BOLD);
  } else {
    std::vector<FlashcardUi::Row> rows(items.size());
    std::vector<std::string> titles(items.size());
    std::vector<std::string> subs(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
      const Item& it = items[i];
      auto& row = rows[i];
      if (it.isFolder) {
        bool mixed = false;
        const FlashcardDeckState s = folderState(it.folder, mixed);
        titles[i] = it.folder + "/";
        row.subtitle = tr(STR_FLASHCARD_ALL_IN_FOLDER);
        row.value = mixed ? tr(STR_FLASHCARD_MIXED) : stateName(s);
      } else {
        const auto& e = entries[it.entry];
        titles[i] = e.folder.empty() ? e.name : "   " + e.name;
        char buf[96];
        if (e.truncated) {
          snprintf(buf, sizeof(buf), "%s", tr(STR_FLASHCARD_LINEAR_ONLY));
        } else if (e.state == FlashcardDeckState::Active) {
          snprintf(buf, sizeof(buf), "%s %u \xC2\xB7 %s: %u \xC2\xB7 %u %s", tr(STR_FLASHCARD_SHARE),
                   static_cast<unsigned>(e.share), tr(STR_FLASHCARD_TODAY_LC), static_cast<unsigned>(e.newToday),
                   static_cast<unsigned>(e.stats.newCards), tr(STR_FLASHCARD_NEW_LC));
        } else {
          snprintf(buf, sizeof(buf), "%s", stateDesc(e.state));
        }
        subs[i] = buf;
        row.subtitle = subs[i].c_str();
        row.value = e.truncated ? "" : stateName(e.state);
      }
      row.title = titles[i].c_str();
    }
    FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, rows.data(), static_cast<int>(rows.size()),
                          selectedIndex);
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void FlashcardDeckOptionsActivity::renderDeck(const int top, const int height) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const auto& e = entries[deckEntry];
  const bool active = e.state == FlashcardDeckState::Active;

  FlashcardUi::Row rows[2];
  rows[0].title = tr(STR_FLASHCARD_STATE);
  rows[0].value = stateName(e.state);
  rows[0].subtitle = stateDesc(e.state);
  char share[16];
  char sub[48];
  if (active) {
    snprintf(share, sizeof(share), editingShare ? "[ %u ]" : "%u", static_cast<unsigned>(e.share));
    snprintf(sub, sizeof(sub), "%s: %u", tr(STR_FLASHCARD_TODAY_LC), static_cast<unsigned>(e.newToday));
    rows[1].title = tr(STR_FLASHCARD_SHARE);
    rows[1].value = share;
    rows[1].subtitle = sub;
  }
  const int count = active ? 2 : 1;
  const int rowsH =
      std::min(height, 3 * (20 + renderer.getLineHeight(UI_12_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) + 2));
  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, rowsH}, rows, count, deckRow);

  char info[96];
  snprintf(info, sizeof(info), "%u %s \xC2\xB7 %u %s \xC2\xB7 %u %s", static_cast<unsigned>(e.stats.totalCards),
           tr(STR_FLASHCARD_CARDS), static_cast<unsigned>(e.stats.newCards), tr(STR_FLASHCARD_NEW_LC),
           static_cast<unsigned>(e.stats.dueCards), tr(STR_FLASHCARD_DUE));
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding + 10, top + rowsH + 8, info);
}
