#include "FlashcardHomeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "FlashcardLinearBrowserActivity.h"
#include "FlashcardSettingsActivity.h"
#include "FlashcardSpacedListActivity.h"
#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ROW_SPACED = 0;
constexpr int ROW_LINEAR = 1;
constexpr int ROW_SETTINGS = 2;
constexpr int ROW_COUNT = 3;
}  // namespace

void FlashcardHomeActivity::onEnter() {
  Activity::onEnter();
  FLASHCARD_STORE.loadFromFile();  // missing file on first use: defaults stay
  requestUpdate();
}

void FlashcardHomeActivity::loop() {
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::FLASHCARDS);
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;

  const auto back = [this](const ActivityResult&) { requestUpdate(); };
  switch (selectedIndex) {
    case ROW_SPACED:
      startActivityForResult(std::make_unique<FlashcardSpacedListActivity>(renderer, mappedInput), back);
      break;
    case ROW_LINEAR:
      startActivityForResult(std::make_unique<FlashcardLinearBrowserActivity>(renderer, mappedInput), back);
      break;
    default:
      startActivityForResult(std::make_unique<FlashcardSettingsActivity>(renderer, mappedInput), back);
      break;
  }
}

void FlashcardHomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLASHCARDS));

  FlashcardUi::Row rows[ROW_COUNT];
  rows[ROW_SPACED].title = tr(STR_FLASHCARD_SPACED);
  rows[ROW_SPACED].subtitle = tr(STR_FLASHCARD_SPACED_DESC);
  rows[ROW_LINEAR].title = tr(STR_FLASHCARD_LINEAR);
  rows[ROW_LINEAR].subtitle = tr(STR_FLASHCARD_LINEAR_DESC);
  rows[ROW_SETTINGS].title = tr(STR_FLASHCARD_SETTINGS);

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, rows, ROW_COUNT, selectedIndex);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
