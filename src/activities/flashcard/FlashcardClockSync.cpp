#include "FlashcardClockSync.h"

#include <Arduino.h>
#include <HalClock.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include <algorithm>
#include <ctime>

#include "FlashcardSrs.h"
#include "FlashcardStore.h"

namespace FlashcardClockSync {

FlashcardClock::State state() {
  const int64_t now = static_cast<int64_t>(time(nullptr));
  // A device with an RTC (X3) set the clock from it at boot and keeps time
  // through sleep: a plausible date needs no confirmation and no new sync.
  if (halClock.isAvailable() && now >= FlashcardClock::MIN_VALID_UNIX) return FlashcardClock::State::Ok;
  return FlashcardClock::evaluate(now, FLASHCARD_STORE.lastSyncUnix);
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
      FLASHCARD_STORE.lastSeenDay =
          FlashcardClock::dayIndex(static_cast<int64_t>(now), FLASHCARD_STORE.utcOffsetMinutes);
      FLASHCARD_STORE.lastNtpDay = FLASHCARD_STORE.lastSeenDay;
      if (!FLASHCARD_STORE.saveToFile()) {
        LOG_ERR("FCLK", "Clock synced but flashcard.json could not be saved");
      }
      halClock.setRtcFromSystemTime();  // devices with an RTC keep it through the next power cut
      LOG_INF("FCLK", "Clock synced: %lld", static_cast<long long>(now));
      return true;
    }
    delay(100);
  }

  LOG_ERR("FCLK", "NTP sync timed out");
  return false;
}

void noteToday() {
  const uint32_t today = FlashcardSrs::getToday();
  if (today == 0 || today == FLASHCARD_STORE.lastSeenDay) return;
  FLASHCARD_STORE.lastSeenDay = today;
  FLASHCARD_STORE.saveToFile();
}

uint32_t suggestedDay() {
  uint32_t day = FLASHCARD_STORE.lastSeenDay;
  if (FLASHCARD_STORE.lastSyncUnix != 0) {
    day = std::max(day, FlashcardClock::dayIndex(static_cast<int64_t>(FLASHCARD_STORE.lastSyncUnix),
                                                 FLASHCARD_STORE.utcOffsetMinutes));
  }
  return day;
}

bool setDay(const uint32_t day) {
  const int64_t unix = FlashcardClock::noonOfDay(day, FLASHCARD_STORE.utcOffsetMinutes);
  if (day == 0 || unix < FlashcardClock::MIN_VALID_UNIX) return false;
  // An RTC clock already on that day keeps its real hour.
  const int64_t now = static_cast<int64_t>(time(nullptr));
  if (halClock.isAvailable() && now >= FlashcardClock::MIN_VALID_UNIX &&
      FlashcardClock::dayIndex(now, FLASHCARD_STORE.utcOffsetMinutes) == day) {
    FLASHCARD_STORE.lastSeenDay = day;
    FLASHCARD_STORE.saveToFile();
    return true;
  }
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(unix);
  if (settimeofday(&tv, nullptr) != 0) {
    LOG_ERR("FCLK", "settimeofday failed");
    return false;
  }
  // The RTC keeps a date set by hand too (at noon: a Wi-Fi sync sets the hour).
  halClock.setRtcFromSystemTime();
  FLASHCARD_STORE.lastSyncUnix = static_cast<uint32_t>(unix);
  FLASHCARD_STORE.lastSeenDay = day;
  if (!FLASHCARD_STORE.saveToFile()) LOG_ERR("FCLK", "Date set but flashcard.json could not be saved");
  LOG_INF("FCLK", "Date confirmed by hand: day %lu", static_cast<unsigned long>(day));
  return true;
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
