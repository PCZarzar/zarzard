#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>

// Flashcard preferences and daily counters, persisted in
// /.crosspoint/flashcard.json. Loaded when the flashcard app opens.
class FlashcardStore : public PersistableStore<FlashcardStore> {
  friend class PersistableStore<FlashcardStore>;
  FlashcardStore() = default;

 public:
  static constexpr uint8_t NEW_PER_DAY_MIN = 5;
  static constexpr uint8_t NEW_PER_DAY_MAX = 50;
  static constexpr uint8_t MAX_REVIEW_MIN = 25;
  static constexpr uint8_t MAX_REVIEW_MAX = 250;
  static constexpr int16_t UTC_OFFSET_MIN_MINUTES = -12 * 60;
  static constexpr int16_t UTC_OFFSET_MAX_MINUTES = 14 * 60;

  static const char* getFilePath() { return "/.crosspoint/flashcard.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  uint8_t newPerDay = 10;           // new cards per day, across all decks
  uint8_t maxReviewPerDay = 250;    // cap per deck session
  uint8_t lastMode = 0;             // last review mode picked: 0 = spaced, 1 = linear
  uint8_t shuffle = 1;              // linear review: reshuffle each pass
  uint16_t newTodayCount = 0;       // new cards first rated on newTodayDate
  uint32_t newTodayDate = 0;        // day of that counter (FlashcardSrs::getToday format)
  int16_t utcOffsetMinutes = -180;  // local day boundary; X4 hides CrossPoint's clock offset
  uint32_t lastSyncUnix = 0;        // last successful NTP sync (UTC seconds), 0 = never
};

#define FLASHCARD_STORE FlashcardStore::getInstance()
