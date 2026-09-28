#include "FlashcardSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "FlashcardClockSync.h"
#include "FlashcardDateActivity.h"
#include "FlashcardSpacedSettingsActivity.h"
#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ROW_SPACED = 0;
constexpr int ROW_UTC_OFFSET = 1;
constexpr int ROW_SHUFFLE = 2;
constexpr int ROW_SYNC = 3;
constexpr int ROW_COUNT = 4;

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
  if (selectedIndex != ROW_UTC_OFFSET) return;
  auto& store = FLASHCARD_STORE;
  store.utcOffsetMinutes = static_cast<int16_t>(std::clamp(store.utcOffsetMinutes + direction * UTC_OFFSET_STEP_MINUTES,
                                                           static_cast<int>(FlashcardStore::UTC_OFFSET_MIN_MINUTES),
                                                           static_cast<int>(FlashcardStore::UTC_OFFSET_MAX_MINUTES)));
  dirty = true;
  requestUpdate();
}

void FlashcardSettingsActivity::loop() {
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
      if (selectedIndex == ROW_SPACED) {
        // Its page saves the store itself; save ours first so neither overwrites the other.
        if (dirty) FLASHCARD_STORE.saveToFile();
        dirty = false;
        startActivityForResult(std::make_unique<FlashcardSpacedSettingsActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { requestUpdate(); });
        return;
      } else if (selectedIndex == ROW_SHUFFLE) {
        FLASHCARD_STORE.shuffle = FLASHCARD_STORE.shuffle ? 0 : 1;
        dirty = true;
      } else if (selectedIndex == ROW_SYNC) {
        // A Wi-Fi sync restarts the device on exit, so save pending edits first.
        if (dirty) FLASHCARD_STORE.saveToFile();
        dirty = false;
        startActivityForResult(std::make_unique<FlashcardDateActivity>(renderer, mappedInput),
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
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLASHCARD_SETTINGS));

  char raw[16];
  const int offset = store.utcOffsetMinutes;
  snprintf(raw, sizeof(raw), "UTC%c%02d:%02d", offset < 0 ? '-' : '+', std::abs(offset) / 60, std::abs(offset) % 60);
  char utc[24];
  snprintf(utc, sizeof(utc), editing && selectedIndex == ROW_UTC_OFFSET ? "[ %s ]" : "%s", raw);
  char spaced[48];
  snprintf(spaced, sizeof(spaced), "%u %s", static_cast<unsigned>(store.newPerDay), tr(STR_FLASHCARD_NEW_LC));

  FlashcardUi::Row rows[ROW_COUNT];
  rows[ROW_SPACED].title = tr(STR_FLASHCARD_SPACED);
  rows[ROW_SPACED].value = spaced;
  rows[ROW_UTC_OFFSET].title = tr(STR_FLASHCARD_UTC_OFFSET);
  rows[ROW_UTC_OFFSET].value = utc;
  rows[ROW_SHUFFLE].title = tr(STR_FLASHCARD_SHUFFLE);
  rows[ROW_SHUFFLE].value = store.shuffle ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rows[ROW_SYNC].title = tr(STR_FLASHCARD_SYNC_DATE);
  const auto clock = FlashcardClockSync::state();
  rows[ROW_SYNC].subtitle = clock == FlashcardClock::State::Unset   ? tr(STR_FLASHCARD_CLOCK_NOT_SET)
                            : clock == FlashcardClock::State::Stale ? tr(STR_FLASHCARD_SYNC_RECOMMENDED)
                                                                    : tr(STR_FLASHCARD_SYNC_HINT);

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, rows, ROW_COUNT, selectedIndex);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
