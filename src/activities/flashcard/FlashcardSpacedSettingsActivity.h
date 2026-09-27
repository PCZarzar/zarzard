#pragma once
#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Settings > Spaced review: the daily new-card total (split between Active
// decks in proportion to their share), max reviews per deck session, free
// days (no reviews scheduled on them) and the Decks page. Saved on exit.
class FlashcardSpacedSettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool editing = false;
  bool editingFreeDays = false;  // 7-day toggle list is showing
  int freeDayCursor = 0;         // weekday under the cursor, 0 = Sunday
  bool dirty = false;
  bool totalChanged = false;

  // Summary of the decks, for the hint under the total and the Decks row.
  uint32_t shareSum = 0;
  uint16_t activeDecks = 0;
  uint16_t standbyDecks = 0;

  void summarizeDecks();
  void step(int direction);

 public:
  explicit FlashcardSpacedSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardSpacedSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
