#include "FlashcardClock.h"

namespace FlashcardClock {

namespace {
constexpr int64_t EPOCH_2000 = 946684800;
constexpr int64_t SECONDS_PER_DAY = 86400;
}  // namespace

State evaluate(const int64_t nowUnix, const uint32_t lastSyncUnix) {
  if (nowUnix < MIN_VALID_UNIX) return State::Unset;
  if (lastSyncUnix == 0) return State::Stale;
  const int64_t sinceSync = nowUnix - static_cast<int64_t>(lastSyncUnix);
  // A clock that runs behind the last sync by more than a day is not trusted either.
  if (sinceSync < -SECONDS_PER_DAY || sinceSync > STALE_AFTER_SECONDS) return State::Stale;
  return State::Ok;
}

int64_t noonOfDay(const uint32_t dayIndex, const int16_t offsetMinutes) {
  return EPOCH_2000 + static_cast<int64_t>(dayIndex) * SECONDS_PER_DAY + 12 * 3600 -
         static_cast<int64_t>(offsetMinutes) * 60;
}

uint32_t dayIndex(const int64_t nowUnix, const int16_t offsetMinutes) {
  const int64_t local = nowUnix + static_cast<int64_t>(offsetMinutes) * 60;
  if (local < EPOCH_2000 + SECONDS_PER_DAY) return 0;  // day 0 is reserved for "unset"
  return static_cast<uint32_t>((local - EPOCH_2000) / SECONDS_PER_DAY);
}

uint8_t weekday(const uint32_t dayIndex) { return static_cast<uint8_t>((dayIndex + 6) % 7); }

bool isFreeDay(const uint32_t dayIndex, const uint8_t freeDaysMask) { return (freeDaysMask >> weekday(dayIndex)) & 1; }

uint32_t avoidFreeDays(const uint32_t due, const uint32_t today, const uint16_t interval, const uint8_t freeDaysMask) {
  constexpr uint8_t ALL_DAYS = 0x7F;
  const uint8_t mask = freeDaysMask & ALL_DAYS;
  if (mask == 0 || mask == ALL_DAYS || due <= today || !isFreeDay(due, mask)) return due;

  const uint32_t maxEarly = interval / 3;
  for (uint32_t k = 1; k <= maxEarly && due - k > today; ++k) {
    if (!isFreeDay(due - k, mask)) return due - k;
  }
  for (uint32_t k = 1; k <= 7; ++k) {
    if (!isFreeDay(due + k, mask)) return due + k;
  }
  return due;  // unreachable: at least one day of the week is a study day
}

}  // namespace FlashcardClock
