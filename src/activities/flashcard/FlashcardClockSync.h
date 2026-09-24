#pragma once
#include <cstdint>

#include "FlashcardClock.h"

// Device side of FlashcardClock: system clock + FlashcardStore.
namespace FlashcardClockSync {

FlashcardClock::State state();

// Sets the system clock from NTP and records the sync in FlashcardStore.
// WiFi must already be connected. Blocks for up to ~5 s.
bool syncFromNtp();

}  // namespace FlashcardClockSync
