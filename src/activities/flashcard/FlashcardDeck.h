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
  // flags: saved in the CSV's optional seventh column ('m', 's'), except
  // DELETED, which only exists in RAM until the deck is saved without the card.
  static constexpr uint8_t FLAG_MARKED = 1;     // set aside by the user to decide later
  static constexpr uint8_t FLAG_SUSPENDED = 2;  // left out of every review, kept in the deck
  static constexpr uint8_t FLAG_DELETED = 4;

  uint16_t id = 0;
  std::string frontContent;  // raw stored content (may contain /n and \ hints \)
  std::string backContent;   // raw stored back text
  SrsState srs;
  uint8_t flags = 0;
  uint32_t offset = 0;  // where the card's record starts in the CSV it was loaded from

  // Split frontContent into main text and hint in one pass (both trimmed,
  // "\\" unescaped, /n replaced by newline). Hint is empty if absent.
  void parseFront(std::string& main, std::string& hint) const;

  std::string frontMain() const;
  std::string frontHint() const;

  bool isNew() const { return srs.dueDate == 0; }
  bool isDue(uint32_t today) const { return !isNew() && srs.dueDate <= today; }
  bool isMarked() const { return flags & FLAG_MARKED; }
  bool isSuspended() const { return flags & FLAG_SUSPENDED; }
  // Takes part in reviews: not suspended, not deleted.
  bool isActive() const { return !(flags & (FLAG_SUSPENDED | FLAG_DELETED)); }
};

// newCards and dueCards count active cards only; totalCards counts every card.
struct DeckStats {
  uint16_t totalCards = 0;
  uint16_t newCards = 0;
  uint16_t dueCards = 0;
  uint16_t suspendedCards = 0;
  uint16_t markedCards = 0;
};

class HalFile;

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
  // Cards flagged DELETED are left out. Refuses to write a truncated deck
  // (would drop the cards past the limit).
  bool saveToCsv(const char* path) const;

  // Rewrites the CSV at `path` one card at a time, without loading the deck:
  // edit(card, ctx) may change a card or flag it DELETED (left out). A few KB
  // of RAM whatever the deck size, so it also works on decks over
  // MAX_CARDS_PER_DECK. False if the file cannot be read or written (it is then
  // left as it was).
  using CardEdit = void (*)(FlashcardCard& card, void* ctx);
  static bool rewriteCsv(const char* path, CardEdit edit, void* ctx);

  // Hash of every card's id, front and back (not the SRS columns nor flags), so
  // saved linear progress can tell whether the deck's cards changed.
  uint32_t contentFingerprint() const;
  // Same hash over some cards only (Linear review skips suspended cards); equal
  // to contentFingerprint() when `indices` lists every card in order.
  uint32_t fingerprintOf(const std::vector<size_t>& indices) const;

  // Indices of the cards that take part in reviews, in deck order.
  std::vector<size_t> activeIndices() const;
  // Index of the card whose record started at `offset` when loaded; -1 if none.
  int findByOffset(uint32_t offset) const;

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

  // Save helpers shared by saveToCsv and rewriteCsv: open `path`.tmp for
  // writing (restoring a .tmp left by an interrupted save first), write one
  // card line, then replace `path` with the .tmp (or drop it when !ok).
  static bool openTmp(const char* path, const std::string& tmpPath, HalFile& out);
  static bool writeCardLine(HalFile& out, const FlashcardCard& card, std::string& scratch);
  static bool commitTmp(const char* path, const std::string& tmpPath, bool ok);

  // Convert "my_deck.csv" -> "my deck".
  static std::string fileNameToDisplayName(const std::string& filename);
};
