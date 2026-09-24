#pragma once
#include <string>

#include "FlashcardClock.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Shown after a deck is confirmed: Spaced (SM-2) or Linear review, the
// Linear shuffle toggle, and the date sync Spaced review depends on.
// Returns MenuResult{action = ReviewMode value}; cancelled on Back.
class FlashcardModeSelectActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::string deckName;
  bool deckTooLarge;  // over MAX_CARDS_PER_DECK: Spaced could not save it
  int selectedIndex = 0;
  FlashcardClock::State clockState = FlashcardClock::State::Unset;

  void refreshClockState();

 public:
  FlashcardModeSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& deckName,
                              bool deckTooLarge)
      : Activity("FlashcardModeSelect", renderer, mappedInput), deckName(deckName), deckTooLarge(deckTooLarge) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
