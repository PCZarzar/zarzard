#include "FlashcardDateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "FlashcardChart.h"
#include "FlashcardClock.h"
#include "FlashcardClockSync.h"
#include "FlashcardClockSyncActivity.h"
#include "FlashcardSrs.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* weekdayName(const int w) {
  switch (w) {
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
}  // namespace

void FlashcardDateActivity::onEnter() {
  Activity::onEnter();
  // Never before 2026-01-01 nor before the last day the date was known.
  const uint32_t first = FlashcardClock::dayIndex(FlashcardClock::MIN_VALID_UNIX, 0);
  const uint32_t known = FlashcardClockSync::suggestedDay();
  minDay = std::max(first, known);
  const uint32_t now = FlashcardSrs::getToday();  // when the date is known, start from it
  day = now >= minDay ? now : minDay;
  option = 0;
  requestUpdate();
}

void FlashcardDateActivity::changeDay(const int delta) {
  if (delta < 0 && day <= minDay) return;
  day = static_cast<uint32_t>(static_cast<int64_t>(day) + delta);
  requestUpdate();
}

void FlashcardDateActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  // Front Left/Right: the day (hold to move fast). Side Up/Down: the option.
  buttonNavigator.onPressAndContinuous({Button::Left}, [this] { changeDay(-1); });
  buttonNavigator.onPressAndContinuous({Button::Right}, [this] { changeDay(+1); });
  if (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Down)) {
    option = 1 - option;
    requestUpdate();
    return;
  }
  if (!mappedInput.wasReleased(Button::Confirm)) return;
  if (option == 0) {
    FlashcardClockSync::setDay(day);
    finish();
    return;
  }
  startActivityForResult(std::make_unique<FlashcardClockSyncActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { finish(); });
}

void FlashcardDateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLASHCARD_DATE_TITLE));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  renderer.drawCenteredText(SMALL_FONT_ID, y, tr(STR_FLASHCARD_DATE_HINT));
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;

  // Weekday, then the date in large type between two arrows.
  renderer.drawCenteredText(UI_12_FONT_ID, y, weekdayName(FlashcardClock::weekday(day)), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 4;
  char date[16];
  FlashcardSrs::formatDateStr(day, date, sizeof(date));
  const int dateW = renderer.getTextWidth(NOTOSERIF_24_FONT_ID, date);
  renderer.drawCenteredText(NOTOSERIF_24_FONT_ID, y, date);
  const int arrowY = y + renderer.getFontAscenderSize(NOTOSERIF_24_FONT_ID) / 2 + 6;
  if (day > minDay) FlashcardChart::drawArrow(renderer, (pageWidth - dateW) / 2 - 28, arrowY, 18, true);
  FlashcardChart::drawArrow(renderer, (pageWidth + dateW) / 2 + 28, arrowY, 18, false);
  y += renderer.getLineHeight(NOTOSERIF_24_FONT_ID);
  if (day == minDay && FlashcardClockSync::suggestedDay() == day) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, tr(STR_FLASHCARD_LAST_KNOWN_DAY));
  }
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 2;

  FlashcardUi::Row rows[2];
  rows[0].title = tr(STR_FLASHCARD_CONFIRM_DATE);
  rows[1].title = tr(STR_FLASHCARD_SYNC_WIFI);
  rows[1].subtitle = tr(STR_FLASHCARD_WIFI_RESTART);
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  FlashcardUi::drawRows(renderer, Rect{0, y, pageWidth, bottom - y}, rows, 2, option);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT),
                                            day > minDay ? tr(STR_FLASHCARD_PREV_DAY) : "", tr(STR_FLASHCARD_NEXT_DAY));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
