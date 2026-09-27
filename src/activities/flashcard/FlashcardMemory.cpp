#include "FlashcardMemory.h"

#include <esp_heap_caps.h>

size_t flashcardLargestFreeBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }
