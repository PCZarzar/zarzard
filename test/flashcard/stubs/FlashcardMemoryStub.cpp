// Host stand-in for the heap query: tests lower it to exercise the
// low-memory paths of the flashcard loaders.
#include <cstddef>
#include <cstdint>

size_t g_testLargestFreeBlock = SIZE_MAX;
size_t flashcardLargestFreeBlock() { return g_testLargestFreeBlock; }
