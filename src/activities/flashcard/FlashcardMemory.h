#pragma once
#include <cstddef>

// Largest block the heap can hand out right now (device: heap_caps; the host
// tests provide their own definition to exercise the low-memory paths).
size_t flashcardLargestFreeBlock();

// Kept free while cards are built: with fields capped at
// FlashcardCsvReader::MAX_FIELD_BYTES, one more card can never exhaust the heap
// (std::string allocation failure aborts the firmware: no exceptions).
constexpr size_t FLASHCARD_HEAP_RESERVE = 24 * 1024;
