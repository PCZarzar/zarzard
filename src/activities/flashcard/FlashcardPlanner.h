#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardDeck.h"

// Spaced review planning: what each deck takes part in, and how the daily
// new-card total is split between decks. Pure logic, tested on the host.
enum class FlashcardDeckState : uint8_t {
  Active = 0,       // new cards and due reviews
  ReviewsOnly = 1,  // due reviews only
  StandBy = 2,      // left out of Spaced review (Linear is unaffected)
};

namespace FlashcardPlanner {

constexpr uint8_t DEFAULT_SHARE = 5;
constexpr uint8_t SHARE_MIN = 1;
constexpr uint8_t SHARE_MAX = 50;

// State of a deck with no saved choice: Active once any of its cards was
// studied in Spaced review, stand-by otherwise (a freshly added deck).
FlashcardDeckState defaultState(const DeckStats& stats);

FlashcardDeckState nextState(FlashcardDeckState s);

struct PlanDeck {
  std::string path;
  FlashcardDeckState state = FlashcardDeckState::Active;
  uint8_t share = DEFAULT_SHARE;  // weight in the split
  uint16_t newCards = 0;          // new cards still in the deck
  bool spacedOk = true;           // false over MAX_CARDS_PER_DECK (Linear only)
};

// New cards a deck gets today (`allot`) and how many of them it already
// introduced (`used`); allot >= used.
struct Allot {
  std::string path;
  uint16_t allot = 0;
  uint16_t used = 0;
};

// Takes part in the split: Active, reviewable in Spaced, with new cards left.
bool eligible(const PlanDeck& d);

// Splits today's new cards. `previous` is today's earlier split (empty on a
// new day): what each deck already used is kept. The rest of `total` (minus
// max(usedToday, sum of used)) goes to eligible decks in proportion to their
// share (Sainte-Laguë, counting what they already used, so the split is fair
// over the day), never beyond the new cards a deck has; ties are broken by
// `seed`. Decks that used cards but are no longer eligible keep their entry.
std::vector<Allot> split(const std::vector<PlanDeck>& decks, const std::vector<Allot>& previous, uint16_t total,
                         uint16_t usedToday, uint32_t seed);

// New cards the deck may still introduce today (0 when it is not in the split).
uint16_t remaining(const std::vector<Allot>& allots, const std::string& path);

// Sum of the shares of the eligible decks: shown against the total.
uint32_t shareSum(const std::vector<PlanDeck>& decks);

}  // namespace FlashcardPlanner
