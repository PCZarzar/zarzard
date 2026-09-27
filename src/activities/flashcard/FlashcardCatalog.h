#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardDeck.h"
#include "FlashcardDeckOptions.h"
#include "FlashcardPlanner.h"

// Decks on the SD card: /flashcard/*.csv plus one level of folders
// (/flashcard/<folder>/*.csv), and the ordering of the Spaced review list.
// No device dependencies beyond HalStorage, so it is tested on the host.
namespace FlashcardCatalog {

struct Entry {
  std::string path;
  std::string name;    // display name from the file name
  std::string folder;  // "" for decks directly in /flashcard
  DeckStats stats;
  bool truncated = false;  // over MAX_CARDS_PER_DECK: Linear only
  FlashcardDeckState state = FlashcardDeckState::Active;
  uint8_t share = FlashcardPlanner::DEFAULT_SHARE;
  uint16_t newToday = 0;  // new cards this deck may still introduce today
};

// Full paths of the decks directly in /flashcard ("") or in one folder.
std::vector<std::string> decksIn(const std::string& folder);

// Every deck: /flashcard first, then each folder in name order.
std::vector<std::string> allDecks();

// Fills in path, display name and folder from the path alone.
void describe(const std::string& path, Entry& out);

// Fills in a deck's stats for `today` by scanning its CSV (no card is kept in
// RAM). False if it cannot be read.
bool load(const std::string& path, uint32_t today, Entry& out);

// Fills each entry's state and share from the saved choices, refreshes
// today's new-card split if needed (true when it changed: save the options)
// and fills newToday. usedToday: the global daily new-card counter.
bool applyPlan(std::vector<Entry>& entries, FlashcardDeckOptions& options, uint32_t today, uint16_t total,
               uint16_t usedToday, uint32_t seed);

// The planner's view of the entries (state, share, new cards).
std::vector<FlashcardPlanner::PlanDeck> planDecks(const std::vector<Entry>& entries);

// Has something to review now: due cards, or new cards left in its share of
// today's total. Stand-by decks and decks over the card limit never count.
bool isPending(const Entry& e);

// Spaced list order: pending decks first, most due first (then most new
// cards, then name); decks with nothing to do after them, by name.
void sortForSpaced(std::vector<Entry>& entries);

}  // namespace FlashcardCatalog
