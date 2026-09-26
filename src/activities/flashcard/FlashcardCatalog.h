#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardDeck.h"

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
};

// Full paths of the decks directly in /flashcard ("") or in one folder.
std::vector<std::string> decksIn(const std::string& folder);

// Every deck: /flashcard first, then each folder in name order.
std::vector<std::string> allDecks();

// Loads a deck to fill in its stats for `today`. False if it cannot be read.
bool load(const std::string& path, uint32_t today, Entry& out);

// Has something to review now: due cards, or new cards while the daily quota
// allows. Decks over the card limit never count (Spaced cannot save them).
bool isPending(const Entry& e, uint16_t newQuota);

// Spaced list order: pending decks first, most due first (then most new
// cards, then name); decks with nothing to do after them, by name.
void sortForSpaced(std::vector<Entry>& entries, uint16_t newQuota);

}  // namespace FlashcardCatalog
