#include "FlashcardHomeActivity.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "FlashcardClockSync.h"
#include "FlashcardDateActivity.h"
#include "FlashcardDeckOptions.h"
#include "FlashcardHistory.h"
#include "FlashcardLinearBrowserActivity.h"
#include "FlashcardPlanRefresh.h"
#include "FlashcardSettingsActivity.h"
#include "FlashcardSpacedListActivity.h"
#include "FlashcardStatsActivity.h"
#include "FlashcardStore.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/themes/zarzard/ZarzardFlashcards.h"
#include "fontIds.h"

namespace {
constexpr int ROW_SPACED = 0;
constexpr int ROW_LINEAR = 1;
constexpr int ROW_STATS = 2;
constexpr int ROW_SETTINGS = 3;
constexpr int ROW_COUNT = 4;

// The X4 forgets the date at every wake; ask once per power-on, not each time
// Flashcards is opened (Back skips it).
bool askedForDate = false;
}  // namespace

void FlashcardHomeActivity::onEnter() {
  Activity::onEnter();
  FLASHCARD_STORE.loadFromFile();  // missing file on first use: defaults stay
  FLASHCARD_DECK_OPTIONS.loadFromFile();
  if (FlashcardClockSync::state() == FlashcardClock::State::Unset && !askedForDate) {
    askedForDate = true;
    startActivityForResult(std::make_unique<FlashcardDateActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             FlashcardClockSync::noteToday();
                             loadFigures();
                             requestUpdate();
                           });
    return;
  }
  FlashcardClockSync::noteToday();
  loadFigures();
  requestUpdate();
}

// Cheap reads only; a stale Spaced count is redone after the first frame.
void FlashcardHomeActivity::loadFigures() {
  cardsToday = -1;
  countPending = false;
  weekTotal = 0;
  std::fill(std::begin(week), std::end(week), 0);
  const uint32_t today = FlashcardSrs::getToday();
  if (today == 0) return;
  if (FLASHCARD_STORE.homeCardsDay == today) {
    cardsToday = FLASHCARD_STORE.homeCardsToday;
  } else {
    countPending = true;
  }
  const uint32_t from = today >= 6 ? today - 6 : 0;
  for (const auto& r : FlashcardHistory::readRange(from, today)) {
    const uint32_t cards = r.cards();
    week[r.day - from] = static_cast<uint16_t>(std::min<uint32_t>(cards, UINT16_MAX));
    weekTotal += cards;
  }
}

void FlashcardHomeActivity::loop() {
  if (countPending && firstRenderDone) {
    flashcardCountCardsToday(FlashcardSrs::getToday());  // reads every deck
    {
      RenderLock lock(*this);
      loadFigures();
    }
    requestUpdate();
    return;
  }
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    FlashcardHistory::flush();
    onGoHome(HomeMenuItem::FLASHCARDS);
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;

  const auto back = [this](const ActivityResult&) {
    loadFigures();
    requestUpdate();
  };
  switch (selectedIndex) {
    case ROW_SPACED:
      startActivityForResult(std::make_unique<FlashcardSpacedListActivity>(renderer, mappedInput), back);
      break;
    case ROW_LINEAR:
      startActivityForResult(std::make_unique<FlashcardLinearBrowserActivity>(renderer, mappedInput), back);
      break;
    case ROW_STATS:
      startActivityForResult(std::make_unique<FlashcardStatsActivity>(renderer, mappedInput), back);
      break;
    default:
      startActivityForResult(std::make_unique<FlashcardSettingsActivity>(renderer, mappedInput), back);
      break;
  }
}

void FlashcardHomeActivity::render(RenderLock&&) {
  ZarzardUi::FlashcardHubView view;
  view.selected = selectedIndex;
  view.title = tr(STR_FLASHCARDS);
  view.spacedLabel = tr(STR_ZZ_SPACED);
  view.linearLabel = tr(STR_ZZ_LINEAR);
  view.statsLabel = tr(STR_FLASHCARD_STATS);
  view.settingsLabel = tr(STR_SETTINGS_TITLE);
  view.cardsToday = cardsToday;
  view.dueLabel = tr(STR_ZZ_DUE_TODAY);
  view.dateUnknownLabel = tr(STR_ZZ_DATE_UNKNOWN);
  std::copy(std::begin(week), std::end(week), std::begin(view.week));
  view.weekTotal = weekTotal;
  view.weekLabel = tr(STR_ZZ_THIS_WEEK);
  view.batteryPercent = powerManager.getBatteryPercentage();
  view.showBatteryPercent = SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;

  renderer.clearScreen();
  ZarzardUi::drawFlashcardHub(renderer, view);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  firstRenderDone = true;
}
