#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "FlashcardSrs.h"

// One day of study. The Spaced fields count Spaced review only, so its
// retention stays meaningful; Linear review only adds volume and time.
struct FlashcardDayRecord {
  uint32_t day = 0;    // days since 2000-01-01
  uint16_t again = 0;  // Spaced ratings
  uint16_t hard = 0;
  uint16_t good = 0;
  uint16_t easy = 0;
  uint16_t newCards = 0;       // Spaced: first rating of a new card
  uint16_t reviewCards = 0;    // Spaced: ratings of cards already in review (interval >= 1)
  uint16_t reviewCorrect = 0;  // ... not rated Again ("true retention")
  uint16_t matureReviews = 0;  // Spaced: ratings of cards with interval >= 21 days
  uint16_t matureCorrect = 0;  // ... not rated Again
  uint16_t linearCards = 0;    // Linear: cards marked
  uint16_t spacedSeconds = 0;  // time on cards: front and back each capped at MAX_SIDE_SECONDS
  uint16_t linearSeconds = 0;

  uint32_t spaced() const { return static_cast<uint32_t>(again) + hard + good + easy; }
  uint32_t cards() const { return spaced() + linearCards; }
  uint32_t seconds() const { return static_cast<uint32_t>(spacedSeconds) + linearSeconds; }
  // Adds other's counters (saturating), keeping `day`.
  void add(const FlashcardDayRecord& other);
};

// Daily study log in /.crosspoint/flashcard_history.bin: fixed-size records,
// one per day, appended as days go by (about 10 KB per year). Today's record
// lives in RAM and is written by flush(), which copies the file through a
// .tmp so a power loss never damages older days.
namespace FlashcardHistory {

constexpr size_t RECORD_BYTES = 28;
constexpr uint16_t MATURE_INTERVAL = 21;
// Time counted per card: up to a minute looking at the front and up to a
// minute reading the back, so a card left on screen does not inflate it.
constexpr uint16_t MAX_SIDE_SECONDS = 60;
constexpr uint16_t MAX_CARD_SECONDS = 2 * MAX_SIDE_SECONDS;
extern const char* const PATH;

void encode(const FlashcardDayRecord& r, uint8_t* out);
bool decode(const uint8_t* in, FlashcardDayRecord& out);

// today == 0 (clock not set): nothing is recorded. intervalBefore is the
// card's interval before this rating (0 = new or relearning).
void recordSpaced(uint32_t today, SrsRating rating, bool wasNew, uint16_t intervalBefore, uint16_t seconds);
void recordLinear(uint32_t today, uint16_t seconds);
// Writes today's record if it changed. False on an SD error (kept in RAM).
bool flush();

// Records with fromDay <= day <= toDay, one per day (merged), ascending.
std::vector<FlashcardDayRecord> readRange(uint32_t fromDay, uint32_t toDay);

struct Summary {
  uint32_t firstDay = 0;  // first day with study, 0 = none
  uint32_t bestDay = 0;   // day with the most cards
  uint32_t bestDayCards = 0;
  uint16_t currentStreak = 0;  // study days in a row up to today (or yesterday)
  uint16_t longestStreak = 0;
};
// Free days (bit w = weekday w, 0 = Sunday) without study do not break a streak.
Summary summarize(uint32_t today, uint8_t freeDaysMask);

// Forgets the RAM state (tests).
void resetForTest();

}  // namespace FlashcardHistory
