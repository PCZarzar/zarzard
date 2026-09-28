#pragma once
#include <cstdint>

#include "FlashcardClock.h"

// Device side of FlashcardClock: system clock + FlashcardStore.
namespace FlashcardClockSync {

FlashcardClock::State state();

// Sets the system clock from NTP and records the sync in FlashcardStore.
// WiFi must already be connected. Blocks for up to ~5 s.
bool syncFromNtp();

// Remembers today as the last day the date was known (saved only when it
// changes). Call when the date is known, e.g. on entering Flashcards.
void noteToday();

// Day offered after a wake: the last day the date was known (or the last
// sync), 0 when there is none.
uint32_t suggestedDay();

// Sets the system clock to local noon of `day`, a date confirmed by hand, and
// records it like a sync. False for day 0 or a date before 2026.
bool setDay(uint32_t day);

}  // namespace FlashcardClockSync
