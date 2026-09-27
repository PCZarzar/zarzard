#pragma once
#include <string>
#include <vector>

#include "FlashcardCatalog.h"
#include "FlashcardClock.h"
#include "FlashcardReviewActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Spaced review: every deck not in stand-by (all folders) in one list that
// orders itself — decks with reviews pending first, most due at the top, then
// "Up to date". New cards per deck come from today's split of the daily total.
// "Review all" chains through the pending decks, most due first; a deck is
// never repeated in a round. Only one deck is loaded in RAM during a session.
class FlashcardSpacedListActivity final : public Activity {
  enum class ItemKind : uint8_t { ReviewAll, SyncDate, UpToDateHeader, Deck };
  struct Item {
    ItemKind kind;
    int deck = -1;  // index into decks for ItemKind::Deck
  };

  ButtonNavigator buttonNavigator;
  std::vector<FlashcardCatalog::Entry> decks;  // sorted for Spaced
  std::vector<Item> items;
  int selectedIndex = 0;
  uint32_t today = 0;
  uint32_t cardsToday = 0;    // due + new allowed today, listed decks
  uint16_t failedDecks = 0;   // CSVs that could not be loaded
  uint16_t standbyDecks = 0;  // not listed (Settings > Spaced review > Decks)
  FlashcardClock::State clockState = FlashcardClock::State::Unset;

  // Current chained round. Cleared whenever control returns to the list.
  std::vector<std::string> visitedPaths;
  std::string offeredNextPath;

  void reload();
  void buildItems();
  int findDeck(const std::string& path) const;
  int firstPendingUnvisited() const;
  void startReview(int deckIndex);
  void openSync();
  FlashcardNextDeck resolveNextDeck();
  static FlashcardNextDeck resolveNextDeckThunk(void* self);

 public:
  explicit FlashcardSpacedListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardSpacedList", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
