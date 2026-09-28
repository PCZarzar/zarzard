#pragma once
#include <cstdint>

// Date handling for Spaced review. Pure functions (no device dependencies) so
// they can be tested on the host; FlashcardClockSync wires them to the system
// clock and FlashcardStore.
//
// The X4 has no RTC chip, and its power circuit cuts the chip off when it
// sleeps: every wake is a power-on with the clock back at 1970 (seen in a
// boot log). Software resets keep the clock. So the date comes from an NTP
// sync or is confirmed by hand after each wake (FlashcardDateActivity).
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

// Unix time of local noon on a day index: what a date confirmed by hand sets
// the clock to (the day, not the hour, is what matters).
int64_t noonOfDay(uint32_t dayIndex, int16_t offsetMinutes);

// Days since 2000-01-01 of the local day containing nowUnix, where local time
// is UTC + offsetMinutes. Same day numbering as FlashcardSrs::parseDateStr().
uint32_t dayIndex(int64_t nowUnix, int16_t offsetMinutes);

// Day of week of a day index: 0 = Sunday ... 6 = Saturday (day 0 = 2000-01-01, a Saturday).
uint8_t weekday(uint32_t dayIndex);

// Free days: bit w of freeDaysMask set = no study on weekday w (bit 0 = Sunday).
bool isFreeDay(uint32_t dayIndex, uint8_t freeDaysMask);

// Moves a freshly scheduled due date off the user's free days (Anki's "Easy
// Days" idea); the SM-2 interval itself is untouched. A due date on a free day
// goes to the nearest study day: earlier when the interval is long enough that
// reviewing up to interval/3 days early is harmless, otherwise later. Due dates
// today or earlier (Again) and an all-days-free mask are left alone.
uint32_t avoidFreeDays(uint32_t due, uint32_t today, uint16_t interval, uint8_t freeDaysMask);

}  // namespace FlashcardClock
