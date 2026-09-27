#pragma once
#include <esp_random.h>

#include <vector>

#include "FlashcardCatalog.h"
#include "FlashcardDeckOptions.h"
#include "FlashcardStore.h"

// Applies the saved deck choices to `entries` and refreshes today's new-card
// split against the daily total and the new cards already introduced today.
// Saves the options when the split changed.
inline void flashcardRefreshPlan(std::vector<FlashcardCatalog::Entry>& entries, const uint32_t today) {
  uint16_t count = FLASHCARD_STORE.newTodayCount;  // copies: read-only peek
  uint32_t date = FLASHCARD_STORE.newTodayDate;
  FlashcardDeck::newCardsAvailable(FLASHCARD_STORE.newPerDay, count, date, today);  // resets them on a new day
  const uint16_t usedToday = (today != 0 && date == today) ? count : 0;
  if (FlashcardCatalog::applyPlan(entries, FLASHCARD_DECK_OPTIONS, today, FLASHCARD_STORE.newPerDay, usedToday,
                                  esp_random())) {
    FLASHCARD_DECK_OPTIONS.saveToFile();
  }
}
