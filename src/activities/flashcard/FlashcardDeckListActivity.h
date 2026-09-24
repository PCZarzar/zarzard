#pragma once
#include <string>
#include <vector>

#include "FlashcardDeck.h"
#include "FlashcardReviewActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Flashcards entry screen: decks in /flashcard/ plus Settings. Owns the
// chained round: after a session it opens the next deck (Linear: next in list
// order; Spaced: next deck with due or allowed new cards, never repeating a
// deck in the round). Only one deck is loaded in RAM at a time.
class FlashcardDeckListActivity final : public Activity {
  struct DeckEntry {
    std::string name;
    std::string path;
    DeckStats stats;
  };

  ButtonNavigator buttonNavigator;
  std::vector<DeckEntry> decks;
  int selectedIndex = 0;
  uint32_t today = 0;
  uint32_t totalDueToday = 0;

  // Current chained round. Cleared whenever control returns to the list.
  std::vector<std::string> visitedPaths;
  FlashcardReviewActivity::ReviewMode roundMode = FlashcardReviewActivity::ReviewMode::Spaced;
  std::string currentPath;
  std::string offeredNextPath;  // deck resolved by the last next-deck offer

  void loadDeckList();
  int findDeck(const std::string& path) const;
  void startReview(int deckIndex, FlashcardReviewActivity::ReviewMode mode);
  FlashcardNextDeck resolveNextDeck();
  static FlashcardNextDeck resolveNextDeckThunk(void* self);

 public:
  explicit FlashcardDeckListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardDeckList", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
