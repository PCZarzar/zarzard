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

uint32_t dayIndex(const int64_t nowUnix, const int16_t offsetMinutes) {
  const int64_t local = nowUnix + static_cast<int64_t>(offsetMinutes) * 60;
  if (local < EPOCH_2000 + SECONDS_PER_DAY) return 0;  // day 0 is reserved for "unset"
  return static_cast<uint32_t>((local - EPOCH_2000) / SECONDS_PER_DAY);
}

uint32_t daysSinceSync(const int64_t nowUnix, const uint32_t lastSyncUnix) {
  if (lastSyncUnix == 0 || nowUnix <= static_cast<int64_t>(lastSyncUnix)) return 0;
  return static_cast<uint32_t>((nowUnix - static_cast<int64_t>(lastSyncUnix)) / SECONDS_PER_DAY);
}

}  // namespace FlashcardClock
