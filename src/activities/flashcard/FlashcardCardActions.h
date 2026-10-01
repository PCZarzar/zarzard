#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardDeck.h"

// What the user can do with one card: set it aside (mark), take it out of
// reviews without losing it (suspend), start it over (reset) or delete it.
enum class FlashcardCardAction : uint8_t { Mark, Unmark, Suspend, Unsuspend, Reset, Delete };

namespace FlashcardCardActions {

// Changes a card in RAM (Delete flags it; saveToCsv then leaves it out).
void apply(FlashcardCard& card, FlashcardCardAction action);

// An action on a card identified by its deck file and record position.
struct Pending {
  std::string path;
  uint32_t offset = 0;
  FlashcardCardAction action = FlashcardCardAction::Mark;
};

// Applies actions to their decks: each deck file is rewritten once, card by
// card (FlashcardDeck::rewriteCsv), so it works whatever the deck size and free
// heap. Actions whose card is no longer there (the file changed) and decks that
// cannot be rewritten (read or write error) are skipped. Returns how many
// actions were applied.
size_t applyToFiles(const std::vector<Pending>& actions);

// Applies `action` to every card of these decks that has any of `flagMask`
// (FlashcardCard::FLAG_MARKED / FLAG_SUSPENDED). Returns how many cards changed;
// `failed` counts decks that could not be rewritten.
size_t applyToFlagged(const std::vector<std::string>& deckPaths, uint8_t flagMask, FlashcardCardAction action,
                      size_t* failed = nullptr);

// Marked and suspended cards of one deck, counted without keeping any card.
struct FlaggedCount {
  std::string path;
  uint16_t marked = 0;
  uint16_t suspended = 0;
};
// Decks with at least one marked or suspended card, in deck order.
std::vector<FlaggedCount> countFlagged(const std::vector<std::string>& deckPaths);

// A marked or suspended card, for the clean-up screen.
struct Flagged {
  std::string path;
  std::string deckName;
  std::string front;  // short display text
  std::string back;
  uint32_t offset = 0;
  uint8_t flags = 0;
};

// Marked or suspended cards of these decks, in deck order (at most `max`).
std::vector<Flagged> collectFlagged(const std::vector<std::string>& deckPaths, size_t max);

}  // namespace FlashcardCardActions
