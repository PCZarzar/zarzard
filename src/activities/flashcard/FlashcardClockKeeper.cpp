#include "FlashcardClockKeeper.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_attr.h>
#include <esp_rtc_time.h>
#include <esp_system.h>
#include <sys/time.h>

#include <cstdio>
#include <ctime>

#include "FlashcardClock.h"

namespace {
constexpr uint32_t MAGIC = 0x46434B31u;  // "FCK1"
constexpr const char* LOG_PATH = "/.crosspoint/fc_clock_log.txt";
constexpr size_t LOG_MAX_BYTES = 8 * 1024;

struct Saved {
  uint32_t magic;
  uint32_t check;
  int64_t unix;
  uint64_t rtcUs;
};

// Survives deep sleep and software resets; garbage after power loss (checked).
RTC_NOINIT_ATTR Saved saved;

uint32_t checkOf(const Saved& s) {
  const uint64_t u = static_cast<uint64_t>(s.unix);
  return s.magic ^ static_cast<uint32_t>(u) ^ static_cast<uint32_t>(u >> 32) ^ static_cast<uint32_t>(s.rtcUs) ^
         static_cast<uint32_t>(s.rtcUs >> 32) ^ 0x5A5A5A5Au;
}

// What afterBoot() saw, written by logBoot() once the SD card works.
struct Boot {
  esp_reset_reason_t reason = ESP_RST_UNKNOWN;
  int64_t timeBefore = 0;
  uint64_t rtcNow = 0;
  bool savedValid = false;
  int64_t savedUnix = 0;
  uint64_t savedRtc = 0;
  bool restored = false;
  int64_t timeAfter = 0;
} boot;

const char* reasonName(const esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    default:
      return "OTHER";
  }
}
}  // namespace

namespace FlashcardClockKeeper {

void beforeSleep() {
  const int64_t now = static_cast<int64_t>(time(nullptr));
  if (now < FlashcardClock::MIN_VALID_UNIX) return;  // nothing worth keeping
  saved.magic = MAGIC;
  saved.unix = now;
  saved.rtcUs = esp_rtc_get_time_us();
  saved.check = checkOf(saved);
}

void afterBoot() {
  boot.reason = esp_reset_reason();
  boot.timeBefore = static_cast<int64_t>(time(nullptr));
  boot.rtcNow = esp_rtc_get_time_us();
  boot.savedValid = saved.magic == MAGIC && saved.check == checkOf(saved);
  if (boot.savedValid) {
    boot.savedUnix = saved.unix;
    boot.savedRtc = saved.rtcUs;
  }
  int64_t restoredUnix = 0;
  if (boot.timeBefore < FlashcardClock::MIN_VALID_UNIX && boot.savedValid &&
      FlashcardClock::restoreAfterSleep(saved.unix, saved.rtcUs, boot.rtcNow, restoredUnix)) {
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(restoredUnix);
    boot.restored = settimeofday(&tv, nullptr) == 0;
  }
  boot.timeAfter = static_cast<int64_t>(time(nullptr));
}

void logBoot() {
  if (Storage.exists(LOG_PATH)) {
    HalFile old;
    if (Storage.openFileForRead("FCKEEP", LOG_PATH, old) && old.size() > LOG_MAX_BYTES) {
      old.close();
      Storage.remove(LOG_PATH);
    }
  }
  HalFile f = Storage.open(LOG_PATH, O_WRITE | O_CREAT | O_APPEND);
  if (!f) {
    LOG_ERR("FCKEEP", "Cannot open %s", LOG_PATH);
    return;
  }
  char line[200];
  const int n = snprintf(line, sizeof(line),
                         "boot=%s time_before=%lld rtc_us=%llu saved=%s saved_time=%lld saved_rtc_us=%llu "
                         "restored=%s time_after=%lld\n",
                         reasonName(boot.reason), static_cast<long long>(boot.timeBefore),
                         static_cast<unsigned long long>(boot.rtcNow), boot.savedValid ? "yes" : "no",
                         static_cast<long long>(boot.savedUnix), static_cast<unsigned long long>(boot.savedRtc),
                         boot.restored ? "yes" : "no", static_cast<long long>(boot.timeAfter));
  if (n > 0) f.write(line, static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1);
  f.close();
  LOG_INF("FCKEEP", "%s", line);
}

}  // namespace FlashcardClockKeeper
