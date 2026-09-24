#include "FlashcardClockSyncActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "FlashcardClockSync.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FlashcardClockSyncActivity::onEnter() {
  Activity::onEnter();
  state = SYNCING;

  if (WiFi.status() == WL_CONNECTED) {
    requestUpdate();
    return;
  }

  shouldTearDownWifiOnExit = true;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             LOG_INF("FCLK", "WiFi selection cancelled before date sync");
                             finish();
                             return;
                           }
                           state = SYNCING;
                           requestUpdate();
                         });
}

void FlashcardClockSyncActivity::onExit() {
  Activity::onExit();
  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FlashcardClockSyncActivity::runSync() {
  if (WiFi.status() != WL_CONNECTED) {
    state = NO_WIFI;
  } else {
    state = FlashcardClockSync::syncFromNtp() ? SUCCESS : FAILED;
  }
  requestUpdate();
}

void FlashcardClockSyncActivity::loop() {
  if (state == SYNCING) {
    // Draw "Syncing..." before blocking on NTP.
    requestUpdateAndWait();
    runSync();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void FlashcardClockSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int midY = renderer.getScreenHeight() / 2;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLASHCARD_SYNC_DATE));

  switch (state) {
    case SYNCING:
      renderer.drawCenteredText(UI_12_FONT_ID, midY, tr(STR_CLOCK_SYNCING));
      break;
    case SUCCESS:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_SYNC_OK), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_FLASHCARD_SYNC_HINT));
      break;
    case NO_WIFI:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_SYNC_NO_WIFI), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_CLOCK_SYNC_NO_WIFI_HINT));
      break;
    case FAILED:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_SYNC_FAIL), true, EpdFontFamily::BOLD);
      break;
  }

  if (state != SYNCING) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
