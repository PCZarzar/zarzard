#include "FlashcardClockSync.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <ctime>

#include "FlashcardSrs.h"
#include "FlashcardStore.h"

namespace FlashcardClockSync {

FlashcardClock::State state() {
  return FlashcardClock::evaluate(static_cast<int64_t>(time(nullptr)), FLASHCARD_STORE.lastSyncUnix);
}

bool syncFromNtp() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("FCLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  // TZ stays UTC so mktime() in FlashcardSrs keeps its day numbering; the
  // local day boundary comes from FlashcardStore::utcOffsetMinutes.
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  constexpr int maxAttempts = 50;  // ~5 s
  for (int i = 0; i < maxAttempts; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      const time_t now = time(nullptr);
      if (static_cast<int64_t>(now) < FlashcardClock::MIN_VALID_UNIX) {
        LOG_ERR("FCLK", "NTP returned an implausible time: %lld", static_cast<long long>(now));
        return false;
      }
      FLASHCARD_STORE.lastSyncUnix = static_cast<uint32_t>(now);
      if (!FLASHCARD_STORE.saveToFile()) {
        LOG_ERR("FCLK", "Clock synced but flashcard.json could not be saved");
      }
      LOG_INF("FCLK", "Clock synced: %lld", static_cast<long long>(now));
      return true;
    }
    delay(100);
  }

  LOG_ERR("FCLK", "NTP sync timed out");
  return false;
}

}  // namespace FlashcardClockSync

namespace FlashcardSrs {

// Day 0 means "unknown": Spaced review is blocked until the clock is synced.
uint32_t getToday() {
  const int64_t now = static_cast<int64_t>(time(nullptr));
  if (FlashcardClock::evaluate(now, FLASHCARD_STORE.lastSyncUnix) == FlashcardClock::State::Unset) return 0;
  return FlashcardClock::dayIndex(now, FLASHCARD_STORE.utcOffsetMinutes);
}

}  // namespace FlashcardSrs
