#pragma once

#include "activities/Activity.h"

// Sets the date Spaced review depends on from NTP (the X4 has no RTC).
// Connects through the normal WiFi selection flow when needed; like
// CrossPoint's ClockSyncActivity it tears WiFi down on exit with a silent
// restart, which keeps the system clock.
class FlashcardClockSyncActivity final : public Activity {
 public:
  explicit FlashcardClockSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardClockSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { SYNCING, SUCCESS, NO_WIFI, FAILED };
  State state = SYNCING;
  bool shouldTearDownWifiOnExit = false;

  void runSync();
};
