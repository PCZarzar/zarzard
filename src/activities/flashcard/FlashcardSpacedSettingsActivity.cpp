#include "FlashcardSpacedSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "FlashcardCatalog.h"
#include "FlashcardDeckOptions.h"
#include "FlashcardDeckOptionsActivity.h"
#include "FlashcardSrs.h"
#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ROW_NEW_PER_DAY = 0;
constexpr int ROW_MAX_REVIEW = 1;
constexpr int ROW_FREE_DAYS = 2;
constexpr int ROW_DECKS = 3;
constexpr int ROW_COUNT = 4;

constexpr int NEW_PER_DAY_STEP = 5;
constexpr int MAX_REVIEW_STEP = 25;
constexpr uint8_t ALL_DAYS = 0x7F;

const char* dayName(const int weekday) {
  switch (weekday) {
    case 0:
      return tr(STR_SUNDAY);
    case 1:
      return tr(STR_MONDAY);
    case 2:
      return tr(STR_TUESDAY);
    case 3:
      return tr(STR_WEDNESDAY);
    case 4:
      return tr(STR_THURSDAY);
    case 5:
      return tr(STR_FRIDAY);
    default:
      return tr(STR_SATURDAY);
  }
}

// First three characters (UTF-8 safe) of each free day, e.g. "Dom, Sáb".
void formatFreeDays(const uint8_t mask, char* out, const size_t size) {
  size_t used = 0;
  out[0] = '\0';
  for (int d = 0; d < 7; ++d) {
    if (!((mask >> d) & 1)) continue;
    const char* name = dayName(d);
    size_t bytes = 0;
    for (int chars = 0; chars < 3 && name[bytes] != '\0'; ++chars) {
      ++bytes;
      while ((static_cast<unsigned char>(name[bytes]) & 0xC0) == 0x80) ++bytes;  // continuation bytes
    }
    const int n = snprintf(out + used, size - used, "%s%.*s", used ? ", " : "", static_cast<int>(bytes), name);
    if (n < 0 || static_cast<size_t>(n) >= size - used) break;
    used += static_cast<size_t>(n);
  }
  if (used == 0) snprintf(out, size, "%s", tr(STR_NONE_OPT));
}
}  // namespace

void FlashcardSpacedSettingsActivity::onEnter() {
  Activity::onEnter();
  summarizeDecks();
  requestUpdate();
}

void FlashcardSpacedSettingsActivity::onExit() {
  if (dirty) FLASHCARD_STORE.saveToFile();
  if (totalChanged) {
    FLASHCARD_DECK_OPTIONS.invalidateSplit();  // today's split follows the new total
    FLASHCARD_DECK_OPTIONS.saveToFile();
  }
  Activity::onExit();
}

void FlashcardSpacedSettingsActivity::summarizeDecks() {
  const uint32_t today = FlashcardSrs::getToday();
  std::vector<FlashcardCatalog::Entry> entries;
  for (const auto& path : FlashcardCatalog::allDecks()) {
    FlashcardCatalog::Entry e;
    if (FlashcardCatalog::load(path, today, e)) entries.push_back(std::move(e));
  }
  activeDecks = 0;
  standbyDecks = 0;
  for (auto& e : entries) {
    e.state = FLASHCARD_DECK_OPTIONS.stateFor(e.path, e.stats);
    e.share = FLASHCARD_DECK_OPTIONS.shareFor(e.path);
    if (e.truncated) continue;
    if (e.state == FlashcardDeckState::StandBy) {
      ++standbyDecks;
    } else {
      ++activeDecks;
    }
  }
  shareSum = FlashcardPlanner::shareSum(FlashcardCatalog::planDecks(entries));
}

void FlashcardSpacedSettingsActivity::step(const int direction) {
  auto& store = FLASHCARD_STORE;
  switch (selectedIndex) {
    case ROW_NEW_PER_DAY:
      store.newPerDay = static_cast<uint8_t>(std::clamp(store.newPerDay + direction * NEW_PER_DAY_STEP,
                                                        static_cast<int>(FlashcardStore::NEW_PER_DAY_MIN),
                                                        static_cast<int>(FlashcardStore::NEW_PER_DAY_MAX)));
      totalChanged = true;
      break;
    case ROW_MAX_REVIEW:
      store.maxReviewPerDay = static_cast<uint8_t>(std::clamp(store.maxReviewPerDay + direction * MAX_REVIEW_STEP,
                                                              static_cast<int>(FlashcardStore::MAX_REVIEW_MIN),
                                                              static_cast<int>(FlashcardStore::MAX_REVIEW_MAX)));
      break;
    default:
      return;
  }
  dirty = true;
  requestUpdate();
}

void FlashcardSpacedSettingsActivity::loop() {
  if (editingFreeDays) {
    buttonNavigator.onNext([this] {
      freeDayCursor = ButtonNavigator::nextIndex(freeDayCursor, 7);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      freeDayCursor = ButtonNavigator::previousIndex(freeDayCursor, 7);
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      const uint8_t toggled = FLASHCARD_STORE.freeDaysMask ^ static_cast<uint8_t>(1u << freeDayCursor);
      if (toggled != ALL_DAYS) {  // at least one study day must remain
        FLASHCARD_STORE.freeDaysMask = toggled;
        dirty = true;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      editingFreeDays = false;
      requestUpdate();
    }
    return;
  }

  if (!editing) {
    buttonNavigator.onNext([this] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ROW_COUNT);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ROW_COUNT);
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedIndex == ROW_FREE_DAYS) {
        editingFreeDays = true;
        freeDayCursor = 0;
      } else if (selectedIndex == ROW_DECKS) {
        startActivityForResult(std::make_unique<FlashcardDeckOptionsActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 summarizeDecks();
                                 requestUpdate();
                               });
        return;
      } else {
        editing = true;
      }
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  // Editing: Next increases, Previous decreases; Confirm/Back leave editing.
  buttonNavigator.onNext([this] { step(+1); });
  buttonNavigator.onPrevious([this] { step(-1); });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    editing = false;
    requestUpdate();
  }
}

void FlashcardSpacedSettingsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& store = FLASHCARD_STORE;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 editingFreeDays ? tr(STR_FLASHCARD_FREE_DAYS) : tr(STR_FLASHCARD_SPACED));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  if (editingFreeDays) {
    FlashcardUi::Row days[7];
    for (int d = 0; d < 7; ++d) {
      days[d].title = dayName(d);
      days[d].value = ((store.freeDaysMask >> d) & 1) ? tr(STR_FLASHCARD_FREE) : "";
    }
    days[0].subtitle = tr(STR_FLASHCARD_FREE_DAYS_HINT);
    FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, days, 7, freeDayCursor);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  char total[16];
  snprintf(total, sizeof(total), editing && selectedIndex == ROW_NEW_PER_DAY ? "[ %d ]" : "%d",
           static_cast<int>(store.newPerDay));
  char maxReview[16];
  snprintf(maxReview, sizeof(maxReview), editing && selectedIndex == ROW_MAX_REVIEW ? "[ %d ]" : "%d",
           static_cast<int>(store.maxReviewPerDay));
  char totalHint[96] = "";
  if (shareSum > 0 && shareSum != store.newPerDay) {
    snprintf(totalHint, sizeof(totalHint), "%s %lu: %s", tr(STR_FLASHCARD_SHARES_SUM),
             static_cast<unsigned long>(shareSum),
             shareSum < store.newPerDay ? tr(STR_FLASHCARD_SHARES_MORE) : tr(STR_FLASHCARD_SHARES_LESS));
  }
  char freeDays[48];
  formatFreeDays(store.freeDaysMask, freeDays, sizeof(freeDays));
  char decks[64];
  snprintf(decks, sizeof(decks), "%u %s \xC2\xB7 %u %s", static_cast<unsigned>(activeDecks),
           tr(STR_FLASHCARD_ACTIVE_LC), static_cast<unsigned>(standbyDecks), tr(STR_FLASHCARD_IN_STANDBY));

  FlashcardUi::Row rows[ROW_COUNT];
  rows[ROW_NEW_PER_DAY].title = tr(STR_FLASHCARD_NEW_PER_DAY);
  rows[ROW_NEW_PER_DAY].value = total;
  rows[ROW_NEW_PER_DAY].subtitle = totalHint;
  rows[ROW_MAX_REVIEW].title = tr(STR_FLASHCARD_MAX_REVIEW);
  rows[ROW_MAX_REVIEW].value = maxReview;
  rows[ROW_FREE_DAYS].title = tr(STR_FLASHCARD_FREE_DAYS);
  rows[ROW_FREE_DAYS].value = freeDays;
  rows[ROW_DECKS].title = tr(STR_FLASHCARD_DECK_LIST);
  rows[ROW_DECKS].subtitle = decks;
  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, rows, ROW_COUNT, selectedIndex);

  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
