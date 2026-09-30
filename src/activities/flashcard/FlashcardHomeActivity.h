#pragma once
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Flashcards entry screen: Spaced review (flat list of every deck), Linear
// review (folder browser), Statistics and Settings.
class FlashcardHomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  // Figures on the tiles: today's Spaced cards (-1 = date unknown) and the
  // last seven days of study.
  int cardsToday = -1;
  bool countPending = false;
  bool firstRenderDone = false;
  uint16_t week[7] = {};
  uint32_t weekTotal = 0;
  void loadFigures();

 public:
  explicit FlashcardHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardHome", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
