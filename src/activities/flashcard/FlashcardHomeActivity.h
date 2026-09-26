#pragma once
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Flashcards entry screen: Spaced review (flat list of every deck), Linear
// review (folder browser) and Settings.
class FlashcardHomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

 public:
  explicit FlashcardHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardHome", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
