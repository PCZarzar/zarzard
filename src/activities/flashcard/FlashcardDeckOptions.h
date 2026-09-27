#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardPlanner.h"

// Spaced review choices per deck (state and share of the daily new cards),
// today's new-card split and the last Linear pass result of each deck, in /.crosspoint/flashcard_decks.json. Decks are
// keyed by path: a renamed CSV starts again from the defaults.
class FlashcardDeckOptions : public PersistableStore<FlashcardDeckOptions> {
  friend class PersistableStore<FlashcardDeckOptions>;
  FlashcardDeckOptions() = default;

 public:
  struct Option {
    std::string path;
    FlashcardDeckState state = FlashcardDeckState::Active;
    uint8_t share = FlashcardPlanner::DEFAULT_SHARE;
  };

  static const char* getFilePath() { return "/.crosspoint/flashcard_decks.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Saved choice, or the default for a deck with these stats.
  FlashcardDeckState stateFor(const std::string& path, const DeckStats& stats) const;
  uint8_t shareFor(const std::string& path) const;
  // Saves an explicit choice; the day's split is recomputed next time.
  void set(const std::string& path, FlashcardDeckState state, uint8_t share);

  // Recomputes today's split when the day changed or a choice changed since
  // it was made. usedToday: new cards introduced today by any deck (the
  // global daily counter). True when it changed (the caller saves).
  bool refreshSplit(const std::vector<FlashcardPlanner::PlanDeck>& decks, uint32_t today, uint16_t total,
                    uint16_t usedToday, uint32_t seed);
  // Marks the split as outdated (choices or the total changed).
  void invalidateSplit() { splitValid = false; }
  // New cards the deck may still introduce today; 0 without a split for today.
  uint16_t newRemaining(const std::string& path, uint32_t today) const;
  // A new card of the deck was rated for the first time today.
  void recordNew(const std::string& path, uint32_t today);
  // Drops the choices of decks that no longer exist.
  bool prune(const std::vector<std::string>& existingPaths);

  // Linear review: result of the last complete pass over a whole deck.
  struct LinearResult {
    std::string path;
    uint16_t passes = 0;       // complete passes done
    uint8_t lastKnownPct = 0;  // share of cards marked Known in the last one
  };
  void recordLinearPass(const std::string& path, size_t known, size_t total);
  const LinearResult* linearResult(const std::string& path) const;

  const std::vector<Option>& getOptions() const { return options; }

 private:
  std::vector<Option> options;
  uint32_t splitDay = 0;  // day of `allots`
  bool splitValid = false;
  std::vector<FlashcardPlanner::Allot> allots;
  std::vector<LinearResult> linear;

  const Option* find(const std::string& path) const;
};

#define FLASHCARD_DECK_OPTIONS FlashcardDeckOptions::getInstance()
