#include "FlashcardStore.h"

#include <algorithm>

void FlashcardStore::toJson(JsonDocument& doc) const {
  doc["newPerDay"] = newPerDay;
  doc["maxReviewPerDay"] = maxReviewPerDay;
  doc["shuffle"] = shuffle;
  doc["newTodayCount"] = newTodayCount;
  doc["newTodayDate"] = newTodayDate;
  doc["utcOffsetMinutes"] = utcOffsetMinutes;
  doc["freeDaysMask"] = freeDaysMask;
  doc["lastSyncUnix"] = lastSyncUnix;
}

bool FlashcardStore::fromJson(JsonVariantConst doc) {
  // Out-of-range values (hand-edited or corrupted file) fall back to a valid value.
  newPerDay = std::clamp(doc["newPerDay"] | static_cast<uint8_t>(10), NEW_PER_DAY_MIN, NEW_PER_DAY_MAX);
  maxReviewPerDay = std::clamp(doc["maxReviewPerDay"] | static_cast<uint8_t>(250), MAX_REVIEW_MIN, MAX_REVIEW_MAX);
  shuffle = (doc["shuffle"] | static_cast<uint8_t>(1)) != 0 ? 1 : 0;
  newTodayCount = doc["newTodayCount"] | static_cast<uint16_t>(0);
  newTodayDate = doc["newTodayDate"] | static_cast<uint32_t>(0);
  utcOffsetMinutes =
      std::clamp(doc["utcOffsetMinutes"] | static_cast<int16_t>(-180), UTC_OFFSET_MIN_MINUTES, UTC_OFFSET_MAX_MINUTES);
  lastSyncUnix = doc["lastSyncUnix"] | static_cast<uint32_t>(0);
  freeDaysMask = (doc["freeDaysMask"] | static_cast<uint8_t>(0)) & 0x7F;
  if (freeDaysMask == 0x7F) freeDaysMask = 0;  // every day free makes no sense: treat as none
  return true;
}
