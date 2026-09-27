#pragma once
#include <string>
#include <vector>

#include "FlashcardSrs.h"

static constexpr size_t MAX_CARDS_PER_DECK = 500;
static constexpr const char* FLASHCARD_DIR = "/flashcard";

// Represents a single flashcard with optional hint syntax in frontContent.
// Front format: "main text \ hint text \" — backslash-delimited hint section.
// "\\" in frontContent is a literal backslash. A stray '\' is literal.
// Line breaks encoded as "/n" in CSV, decoded on access.
struct FlashcardCard {
  uint16_t id = 0;
  std::string frontContent;  // raw stored content (may contain /n and \ hints \)
  std::string backContent;   // raw stored back text
  SrsState srs;

  // Split frontContent into main text and hint in one pass (both trimmed,
  // "\\" unescaped, /n replaced by newline). Hint is empty if absent.
  void parseFront(std::string& main, std::string& hint) const;

  std::string frontMain() const;
  std::string frontHint() const;

  bool isNew() const { return srs.dueDate == 0; }
  bool isDue(uint32_t today) const { return !isNew() && srs.dueDate <= today; }
};

struct DeckStats {
  uint16_t totalCards = 0;
  uint16_t newCards = 0;
  uint16_t dueCards = 0;
};

class FlashcardDeck {
 public:
  // Decode /n sequences to newlines; "\/" is a literal '/' (no newline).
  // Exposed as public static so FlashcardCard member functions can use it.
  static std::string decodeNewlines(const std::string& raw);

  // Load a deck from a CSV on the SD card, streaming (the file is never held
  // whole in RAM). False on a read error, an empty file, or when the cards
  // would not fit in the free heap with FLASHCARD_HEAP_RESERVE to spare
  // (outOfMemory() then) — never an abort.
  bool loadFromCsv(const char* path);
  bool outOfMemory() const { return oom; }

  // Card counts of a deck without keeping any card in RAM (lists, stats).
  // Same results as loadFromCsv() + getStats(today); truncated when the CSV has
  // more than MAX_CARDS_PER_DECK cards. False on a read error.
  static bool scanStats(const char* path, uint32_t today, DeckStats& out, bool& truncated);

  // "my_deck.csv" -> "my deck" for a full path.
  static std::string displayNameForPath(const std::string& path);

  // Atomically write deck to CSV: write to .tmp, remove original, rename.
  // Refuses to write a truncated deck (would drop the cards past the limit).
  bool saveToCsv(const char* path) const;

  // Hash of every card's id, front and back (not the SRS columns), so saved
  // linear progress can tell whether the deck's cards changed.
  uint32_t contentFingerprint() const;

  // True when the CSV had more than MAX_CARDS_PER_DECK cards.
  bool isTruncated() const { return truncated; }

  const std::string& getName() const { return name; }
  const std::string& getPath() const { return filePath; }

  DeckStats getStats(uint32_t today) const;

  // Build review queue: due cards first, then new cards (up to newPerDay).
  // Total queue size capped at maxReview. Returns card indices.
  std::vector<size_t> buildReviewQueue(uint32_t today, uint8_t newPerDay, uint16_t maxReview) const;

  // Daily new-card quota shared by all decks. Starts a new day when today
  // differs from todayDate (resets todayCount). Returns how many new cards
  // may still be introduced today. With today == 0 (clock not set) the
  // counter is left untouched and newPerDay is returned (per-open limit).
  static uint16_t newCardsAvailable(uint8_t newPerDay, uint16_t& todayCount, uint32_t& todayDate, uint32_t today);

  // Cards a Spaced session of a deck with these stats would show.
  static uint16_t spacedSessionSize(const DeckStats& stats, uint16_t newQuota, uint16_t maxReview);

  // Update SRS state for the card at index. Returns false if index out of range.
  bool updateCard(size_t index, const SrsState& newState);

  std::vector<FlashcardCard>& getCards() { return cards; }
  const std::vector<FlashcardCard>& getCards() const { return cards; }

  // Full paths of the .csv files directly inside dir (default FLASHCARD_DIR),
  // sorted. Restores decks orphaned as .csv.tmp by an interrupted save.
  static std::vector<std::string> listDecks(const std::string& dir = FLASHCARD_DIR);

  // Names of the folders directly inside FLASHCARD_DIR (hidden ones skipped), sorted.
  static std::vector<std::string> listFolders();

 private:
  bool truncated = false;
  bool oom = false;
  std::string name;
  std::string filePath;
  std::vector<FlashcardCard> cards;

  // Append field to out, quoting if necessary for CSV compliance.
  static void writeCsvField(std::string& out, const std::string& field);

  // Convert "my_deck.csv" -> "my deck".
  static std::string fileNameToDisplayName(const std::string& filename);
};
