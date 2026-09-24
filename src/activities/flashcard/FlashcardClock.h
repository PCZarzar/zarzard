#pragma once
#include <cstdint>

// Date handling for Spaced review. Pure functions (no device dependencies) so
// they can be tested on the host; FlashcardClockSync wires them to the system
// clock and FlashcardStore.
//
// The X4 has no RTC chip: the system clock is only right after an NTP sync
// done from the flashcard app. ESP-IDF keeps it across deep sleep and software
// resets (RTC timer), but a power-on reset brings it back to 1970.
namespace FlashcardClock {

// 2026-01-01T00:00:00Z. Anything earlier means the clock was never set since
// the last power-on reset.
constexpr int64_t MIN_VALID_UNIX = 1767225600;
// Suggest a new sync after this long (the internal RTC oscillator drifts in sleep).
constexpr int64_t STALE_AFTER_SECONDS = 3 * 86400;

enum class State : uint8_t {
  Unset,  // no trustworthy date: Spaced review is blocked
  Stale,  // usable, but a new sync is recommended
  Ok,
};

State evaluate(int64_t nowUnix, uint32_t lastSyncUnix);

// Days since 2000-01-01 of the local day containing nowUnix, where local time
// is UTC + offsetMinutes. Same day numbering as FlashcardSrs::parseDateStr().
uint32_t dayIndex(int64_t nowUnix, int16_t offsetMinutes);

// Whole days since the last sync (0 if never synced or clock behind it).
uint32_t daysSinceSync(int64_t nowUnix, uint32_t lastSyncUnix);

}  // namespace FlashcardClock
