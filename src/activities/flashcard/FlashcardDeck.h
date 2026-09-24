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
  // Decode /n sequences in raw content to actual newlines.
  // Exposed as public static so FlashcardCard member functions can use it.
  static std::string decodeNewlines(const std::string& raw);

  // Load deck from a CSV file on SD card. Returns false on parse failure.
  bool loadFromCsv(const char* path);

  // Atomically write deck to CSV: write to .tmp, remove original, rename.
  bool saveToCsv(const char* path) const;

  // Copy a CSV file from srcPath into FLASHCARD_DIR and load it.
  bool importCsv(const char* srcPath);

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

  // Spaced round: index of the next deck after `from` in list order
  // (wrapping), skipping visited decks, that has due cards or new cards
  // allowed by newQuota. -1 if none ("All caught up").
  static int nextSpacedDeck(const std::vector<DeckStats>& stats, const std::vector<bool>& visited, int from,
                            uint16_t newQuota);

  // Cards a Spaced session of a deck with these stats would show.
  static uint16_t spacedSessionSize(const DeckStats& stats, uint16_t newQuota, uint16_t maxReview);

  // Update SRS state for the card at index. Returns false if index out of range.
  bool updateCard(size_t index, const SrsState& newState);

  std::vector<FlashcardCard>& getCards() { return cards; }
  const std::vector<FlashcardCard>& getCards() const { return cards; }

  // Return full paths of all .csv files in FLASHCARD_DIR.
  static std::vector<std::string> listDecks();

 private:
  std::string name;
  std::string filePath;
  std::vector<FlashcardCard> cards;

  // Parse one CSV field starting at *ptr, advance ptr past the field separator.
  static std::string parseCsvField(const char*& ptr);

  // Append field to out, quoting if necessary for CSV compliance.
  static void writeCsvField(std::string& out, const std::string& field);

  // Convert "my_deck.csv" -> "my deck".
  static std::string fileNameToDisplayName(const std::string& filename);
};
