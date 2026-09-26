#include "FlashcardSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "FlashcardClockSync.h"
#include "FlashcardClockSyncActivity.h"
#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ROW_NEW_PER_DAY = 0;
constexpr int ROW_MAX_REVIEW = 1;
constexpr int ROW_UTC_OFFSET = 2;
constexpr int ROW_FREE_DAYS = 3;
constexpr int ROW_SHUFFLE = 4;
constexpr int ROW_SYNC = 5;
constexpr int ROW_COUNT = 6;

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

constexpr int NEW_PER_DAY_STEP = 5;
constexpr int MAX_REVIEW_STEP = 25;
constexpr int UTC_OFFSET_STEP_MINUTES = 30;
}  // namespace

void FlashcardSettingsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void FlashcardSettingsActivity::onExit() {
  if (dirty) FLASHCARD_STORE.saveToFile();
  Activity::onExit();
}

void FlashcardSettingsActivity::step(const int direction) {
  auto& store = FLASHCARD_STORE;
  switch (selectedIndex) {
    case ROW_NEW_PER_DAY:
      store.newPerDay = static_cast<uint8_t>(std::clamp(store.newPerDay + direction * NEW_PER_DAY_STEP,
                                                        static_cast<int>(FlashcardStore::NEW_PER_DAY_MIN),
                                                        static_cast<int>(FlashcardStore::NEW_PER_DAY_MAX)));
      break;
    case ROW_MAX_REVIEW:
      store.maxReviewPerDay = static_cast<uint8_t>(std::clamp(store.maxReviewPerDay + direction * MAX_REVIEW_STEP,
                                                              static_cast<int>(FlashcardStore::MAX_REVIEW_MIN),
                                                              static_cast<int>(FlashcardStore::MAX_REVIEW_MAX)));
      break;
    case ROW_UTC_OFFSET:
      store.utcOffsetMinutes =
          static_cast<int16_t>(std::clamp(store.utcOffsetMinutes + direction * UTC_OFFSET_STEP_MINUTES,
                                          static_cast<int>(FlashcardStore::UTC_OFFSET_MIN_MINUTES),
                                          static_cast<int>(FlashcardStore::UTC_OFFSET_MAX_MINUTES)));
      break;
    default:
      return;
  }
  dirty = true;
  requestUpdate();
}

void FlashcardSettingsActivity::loop() {
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
      } else if (selectedIndex == ROW_SHUFFLE) {
        FLASHCARD_STORE.shuffle = FLASHCARD_STORE.shuffle ? 0 : 1;
        dirty = true;
      } else if (selectedIndex == ROW_SYNC) {
        // Restarts the device on exit when it had to turn WiFi on (like
        // CrossPoint's clock sync), so save pending edits first.
        if (dirty) FLASHCARD_STORE.saveToFile();
        dirty = false;
        startActivityForResult(std::make_unique<FlashcardClockSyncActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestUpdate(); });
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

void FlashcardSettingsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& store = FLASHCARD_STORE;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 editingFreeDays ? tr(STR_FLASHCARD_FREE_DAYS) : tr(STR_FLASHCARD_SETTINGS));

  char values[ROW_COUNT][24];
  char raw[16];
  snprintf(raw, sizeof(raw), "%d", static_cast<int>(store.newPerDay));
  snprintf(values[ROW_NEW_PER_DAY], sizeof(values[0]), editing && selectedIndex == ROW_NEW_PER_DAY ? "[ %s ]" : "%s",
           raw);
  snprintf(raw, sizeof(raw), "%d", static_cast<int>(store.maxReviewPerDay));
  snprintf(values[ROW_MAX_REVIEW], sizeof(values[0]), editing && selectedIndex == ROW_MAX_REVIEW ? "[ %s ]" : "%s",
           raw);
  const int offset = store.utcOffsetMinutes;
  snprintf(raw, sizeof(raw), "UTC%c%02d:%02d", offset < 0 ? '-' : '+', std::abs(offset) / 60, std::abs(offset) % 60);
  snprintf(values[ROW_UTC_OFFSET], sizeof(values[0]), editing && selectedIndex == ROW_UTC_OFFSET ? "[ %s ]" : "%s",
           raw);

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

  char freeDays[48];
  formatFreeDays(store.freeDaysMask, freeDays, sizeof(freeDays));

  FlashcardUi::Row rows[ROW_COUNT];
  rows[ROW_NEW_PER_DAY].title = tr(STR_FLASHCARD_NEW_PER_DAY);
  rows[ROW_MAX_REVIEW].title = tr(STR_FLASHCARD_MAX_REVIEW);
  rows[ROW_UTC_OFFSET].title = tr(STR_FLASHCARD_UTC_OFFSET);
  for (int i = ROW_NEW_PER_DAY; i <= ROW_UTC_OFFSET; ++i) rows[i].value = values[i];
  rows[ROW_FREE_DAYS].title = tr(STR_FLASHCARD_FREE_DAYS);
  rows[ROW_FREE_DAYS].value = freeDays;
  rows[ROW_SHUFFLE].title = tr(STR_FLASHCARD_SHUFFLE);
  rows[ROW_SHUFFLE].value = store.shuffle ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rows[ROW_SYNC].title = tr(STR_FLASHCARD_SYNC_DATE);
  const auto clock = FlashcardClockSync::state();
  rows[ROW_SYNC].subtitle = clock == FlashcardClock::State::Unset   ? tr(STR_FLASHCARD_CLOCK_NOT_SET)
                            : clock == FlashcardClock::State::Stale ? tr(STR_FLASHCARD_SYNC_RECOMMENDED)
                                                                    : tr(STR_FLASHCARD_SYNC_HINT);

  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, rows, ROW_COUNT, selectedIndex);

  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
