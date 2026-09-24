#include "FlashcardSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ROW_NEW_PER_DAY = 0;
constexpr int ROW_MAX_REVIEW = 1;
constexpr int ROW_UTC_OFFSET = 2;
constexpr int ROW_COUNT = 3;

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
      editing = true;
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

  FlashcardUi::Row rows[ROW_COUNT];
  rows[ROW_NEW_PER_DAY].title = tr(STR_FLASHCARD_NEW_PER_DAY);
  rows[ROW_MAX_REVIEW].title = tr(STR_FLASHCARD_MAX_REVIEW);
  rows[ROW_UTC_OFFSET].title = tr(STR_FLASHCARD_UTC_OFFSET);
  for (int i = 0; i < ROW_COUNT; ++i) rows[i].value = values[i];

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, rows, ROW_COUNT, selectedIndex);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
