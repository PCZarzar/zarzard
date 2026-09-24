#include "FlashcardModeSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "FlashcardClockSync.h"
#include "FlashcardClockSyncActivity.h"
#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Rows: 0 = Spaced, 1 = Linear (values match ReviewMode), 2 = Shuffle, 3 = Sync date
constexpr int ROW_SPACED = 0;
constexpr int ROW_LINEAR = 1;
constexpr int ROW_SHUFFLE = 2;
constexpr int ROW_SYNC = 3;
constexpr int ROW_COUNT = 4;
}  // namespace

void FlashcardModeSelectActivity::refreshClockState() { clockState = FlashcardClockSync::state(); }

void FlashcardModeSelectActivity::onEnter() {
  Activity::onEnter();
  refreshClockState();
  selectedIndex = FLASHCARD_STORE.lastMode == 1 ? ROW_LINEAR : ROW_SPACED;
  if (selectedIndex == ROW_SPACED && deckTooLarge) selectedIndex = ROW_LINEAR;
  if (selectedIndex == ROW_SPACED && clockState == FlashcardClock::State::Unset) selectedIndex = ROW_SYNC;
  requestUpdate();
}

void FlashcardModeSelectActivity::loop() {
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;

  switch (selectedIndex) {
    case ROW_SHUFFLE:
      FLASHCARD_STORE.shuffle = FLASHCARD_STORE.shuffle ? 0 : 1;
      FLASHCARD_STORE.saveToFile();
      requestUpdate();
      return;
    case ROW_SYNC:
      // The sync screen restarts the device when it tears WiFi down, like
      // CrossPoint's own clock sync; the date survives the restart.
      startActivityForResult(std::make_unique<FlashcardClockSyncActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               refreshClockState();
                               requestUpdate();
                             });
      return;
    case ROW_SPACED:
      if (deckTooLarge) return;  // ratings could not be saved
      if (clockState == FlashcardClock::State::Unset) {
        // Never rate cards against an unknown date: it would write bogus due dates.
        selectedIndex = ROW_SYNC;
        requestUpdate();
        return;
      }
      break;
    default:
      break;
  }

  FLASHCARD_STORE.lastMode = static_cast<uint8_t>(selectedIndex);
  FLASHCARD_STORE.saveToFile();
  MenuResult r;
  r.action = selectedIndex;  // ROW_SPACED / ROW_LINEAR match ReviewMode
  setResult(std::move(r));
  finish();
}

void FlashcardModeSelectActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, deckName.c_str());

  const char* spacedSub = nullptr;
  if (deckTooLarge) {
    spacedSub = tr(STR_FLASHCARD_TOO_MANY_CARDS);
  } else if (clockState == FlashcardClock::State::Unset) {
    spacedSub = tr(STR_FLASHCARD_CLOCK_NOT_SET);
  } else if (clockState == FlashcardClock::State::Stale) {
    spacedSub = tr(STR_FLASHCARD_SYNC_RECOMMENDED);
  }

  FlashcardUi::Row rows[ROW_COUNT];
  rows[ROW_SPACED].title = tr(STR_FLASHCARD_SPACED);
  rows[ROW_SPACED].subtitle = spacedSub;
  rows[ROW_LINEAR].title = tr(STR_FLASHCARD_LINEAR);
  rows[ROW_SHUFFLE].title = tr(STR_FLASHCARD_SHUFFLE);
  rows[ROW_SHUFFLE].value = FLASHCARD_STORE.shuffle ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  rows[ROW_SYNC].title = tr(STR_FLASHCARD_SYNC_DATE);
  rows[ROW_SYNC].subtitle = tr(STR_FLASHCARD_SYNC_HINT);

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, rows, ROW_COUNT, selectedIndex);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
