#pragma once
#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// New cards per day, max reviews per deck session, and the time zone that
// sets when the flashcard day changes. Saved on exit.
class FlashcardSettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool editing = false;
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
