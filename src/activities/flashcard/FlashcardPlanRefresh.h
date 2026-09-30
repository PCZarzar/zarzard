#pragma once
#include <esp_random.h>

#include <algorithm>
#include <cstdint>
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

// Remembers today's Spaced total (due + new) for Home; saves only on a change.
inline void flashcardRememberCardsToday(const uint32_t today, const uint32_t cards) {
  if (today == 0) return;
  const uint16_t value = static_cast<uint16_t>(std::min<uint32_t>(cards, UINT16_MAX));
  if (FLASHCARD_STORE.homeCardsDay == today && FLASHCARD_STORE.homeCardsToday == value) return;
  FLASHCARD_STORE.homeCardsDay = today;
  FLASHCARD_STORE.homeCardsToday = value;
  FLASHCARD_STORE.saveToFile();
}

// Counts today's Spaced total over every deck (stand-by and oversized decks do
// not count), the same sum the Spaced list shows, and remembers it.
inline uint32_t flashcardCountCardsToday(const uint32_t today) {
  if (today == 0) return 0;
  const auto paths = FlashcardCatalog::allDecks();
  std::vector<FlashcardCatalog::Entry> all;
  all.reserve(paths.size());
  for (const auto& path : paths) {
    FlashcardCatalog::Entry e;
    if (FlashcardCatalog::load(path, today, e)) all.push_back(std::move(e));
  }
  flashcardRefreshPlan(all, today);
  uint32_t cards = 0;
  for (const auto& e : all) {
    if (e.state == FlashcardDeckState::StandBy || e.truncated) continue;
    cards += e.stats.dueCards + std::min<uint32_t>(e.stats.newCards, e.newToday);
  }
  flashcardRememberCardsToday(today, cards);
  return cards;
}
