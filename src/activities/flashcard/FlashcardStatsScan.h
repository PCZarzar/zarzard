#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardDeck.h"

// Linear session over the difficult cards of every deck (progress key).
constexpr const char* FLASHCARD_HARD_KEY = "/flashcard/.hard";

// Card phases of the Spaced review, as in Anki.
struct FlashcardPhases {
  uint32_t newCards = 0;
  uint32_t learning = 0;  // studied, interval < 21 days
  uint32_t mature = 0;    // interval >= 21 days
  uint32_t total() const { return newCards + learning + mature; }
  void add(const FlashcardPhases& o) {
    newCards += o.newCards;
    learning += o.learning;
    mature += o.mature;
  }
};

// A card with a low ease: missed many times in Spaced review.
struct FlashcardHardCard {
  std::string front;  // short display text
  std::string back;
  std::string deckName;
  std::string path;     // deck CSV
  uint32_t offset = 0;  // record position in the CSV
  uint16_t ease = 0;
  uint16_t interval = 0;
};

// Statistics read straight from the deck CSVs, streaming (no deck is loaded).
namespace FlashcardStatsScan {

constexpr int FORECAST_DAYS = 30;
constexpr size_t MAX_HARD = 40;
constexpr uint16_t HARD_EASE = 150;  // about five Again ratings below the starting 250%
constexpr size_t TEXT_BYTES = 120;

struct DeckResult {
  DeckStats stats;  // same counts as FlashcardDeck::scanStats()
  bool truncated = false;
  FlashcardPhases phases;
};

// Scans the first MAX_CARDS_PER_DECK cards of a deck (what Spaced review
// uses). When non-null, forecast (FORECAST_DAYS counters; index 0 = due today
// or overdue) is added to, and hard cards are merged into `hard` (worst first,
// at most MAX_HARD). False on a read error.
bool scanDeck(const std::string& path, uint32_t today, DeckResult& out, uint32_t* forecast,
              std::vector<FlashcardHardCard>* hard);

// Difficult cards of these decks, worst first (at most MAX_HARD).
std::vector<FlashcardHardCard> collectHard(const std::vector<std::string>& deckPaths);

// Lower ease first, then shorter interval, then deck and position.
bool harder(const FlashcardHardCard& a, const FlashcardHardCard& b);

// Text for one list line: newlines become spaces, cut at TEXT_BYTES on a
// UTF-8 character boundary.
std::string shortText(const std::string& s);

}  // namespace FlashcardStatsScan
