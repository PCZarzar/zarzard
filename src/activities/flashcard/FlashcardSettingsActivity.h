#pragma once
#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// New cards per day, max reviews per deck session, the time zone that sets
// when the flashcard day changes, free days (no reviews scheduled on them), the
// Linear shuffle toggle and the date sync Spaced review needs (the X4 has no
// RTC). Saved on exit.
class FlashcardSettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool editing = false;
  bool editingFreeDays = false;  // 7-day toggle list is showing
  int freeDayCursor = 0;         // weekday under the cursor, 0 = Sunday
  bool dirty = false;

  void step(int direction);

 public:
  explicit FlashcardSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
