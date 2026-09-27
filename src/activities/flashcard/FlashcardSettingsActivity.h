#pragma once
#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Flashcard settings: the Spaced review page (daily new cards, decks, free
// days...), the time zone that sets when the flashcard day changes, the Linear
// shuffle toggle and the date sync Spaced review needs (the X4 has no RTC).
// Saved on exit.
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
