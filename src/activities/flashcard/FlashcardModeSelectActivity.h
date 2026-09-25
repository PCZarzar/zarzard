#pragma once
#include <string>

#include "FlashcardClock.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Shown after a deck is confirmed: Spaced (SM-2) or Linear review, the
// Linear shuffle toggle, and the date sync Spaced review depends on.
// Returns MenuResult{action = ReviewMode value, or ACTION_LINEAR_CONTINUE to
// resume the saved linear pass}; cancelled on Back.
class FlashcardModeSelectActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::string deckName;
  std::string deckPath;
  bool deckTooLarge;  // over MAX_CARDS_PER_DECK: Spaced could not save it
  int selectedIndex = 0;
  FlashcardClock::State clockState = FlashcardClock::State::Unset;
  // Saved linear pass: offered as Continue / Restart.
  bool hasSavedPass = false;
  char savedPassLabel[48] = "";
  bool choosingLinearStart = false;  // Continue / Restart sub-list is showing
  int linearChoice = 0;              // 0 = Continue, 1 = Restart

  void finishWith(int action);

  void refreshClockState();

 public:
  static constexpr int ACTION_LINEAR_CONTINUE = 2;

  FlashcardModeSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& deckName,
                              const std::string& deckPath, bool deckTooLarge)
      : Activity("FlashcardModeSelect", renderer, mappedInput),
        deckName(deckName),
        deckPath(deckPath),
        deckTooLarge(deckTooLarge) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
