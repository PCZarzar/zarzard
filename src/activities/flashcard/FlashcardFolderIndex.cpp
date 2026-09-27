#include "FlashcardFolderIndex.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>

#include "FlashcardCsv.h"
#include "FlashcardMemory.h"

namespace {
constexpr const char* TAG = "FlashcardIndex";

void mix(uint32_t& h, const void* data, size_t len) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
}
}  // namespace

bool FlashcardFolderIndex::build(const std::vector<std::string>& deckPaths) {
  files.clear();
  refs.clear();
  hash = 2166136261u;  // FNV-1a, same mixing as FlashcardDeck::contentFingerprint()
  truncated = false;

  FlashcardCsvReader::Record rec;
  for (const auto& path : deckPaths) {
    if (truncated) break;
    HalFile file;
    if (!Storage.openFileForRead(TAG, path, file)) continue;
    const size_t fsize = file.size();
    FlashcardCsvReader reader(file);
    reader.skipHeader();
    const size_t firstRef = refs.size();
    const uint16_t fileIndex = static_cast<uint16_t>(files.size());
    uint16_t nextId = 1;
    while (reader.next(rec)) {
      if (refs.size() >= MAX_CARDS) {
        truncated = true;
        break;
      }
      // Growing the vector doubles it: make sure that block exists with room to spare.
      if (refs.size() == refs.capacity() &&
          flashcardLargestFreeBlock() < (refs.size() * 2 + 16) * sizeof(Ref) + FLASHCARD_HEAP_RESERVE) {
        LOG_ERR(TAG, "Low memory, folder index stops at %u cards", (unsigned)refs.size());
        truncated = true;
        break;
      }
      const uint16_t id = rec.id.empty() ? nextId : static_cast<uint16_t>(atoi(rec.id.c_str()));
      nextId = id + 1;
      mix(hash, &id, sizeof(id));
      mix(hash, rec.front.c_str(), rec.front.size() + 1);
      mix(hash, rec.back.c_str(), rec.back.size() + 1);
      refs.push_back(Ref{rec.offset, fileIndex});
    }
    const bool complete = truncated || (!reader.failed() && reader.bytesRead() == fsize);
    if (!complete) {
      // A deck that could not be read whole is left out entirely.
      LOG_ERR(TAG, "Read error, skipping %s", path.c_str());
      refs.resize(firstRef);
      continue;
    }
    if (refs.size() > firstRef) files.push_back(path);
  }
  return !refs.empty();
}

bool FlashcardFolderIndex::buildFromRefs(const std::vector<CardRef>& cards) {
  files.clear();
  refs.clear();
  hash = 2166136261u;
  truncated = false;
  const size_t n = std::min(cards.size(), MAX_CARDS);
  refs.reserve(n);
  FlashcardCard card;
  for (size_t i = 0; i < n; ++i) {
    const CardRef& c = cards[i];
    auto it = std::find(files.begin(), files.end(), c.path);
    const uint16_t fileIndex = static_cast<uint16_t>(it - files.begin());
    if (it == files.end()) files.push_back(c.path);
    refs.push_back(Ref{c.offset, fileIndex});
    // Checks the position still points at a card (the CSV may have changed).
    if (!loadCard(refs.size() - 1, card)) {
      refs.pop_back();
      continue;
    }
    mix(hash, &c.offset, sizeof(c.offset));
    mix(hash, card.frontContent.c_str(), card.frontContent.size() + 1);
    mix(hash, card.backContent.c_str(), card.backContent.size() + 1);
  }
  truncated = cards.size() > n;
  return !refs.empty();
}

bool FlashcardFolderIndex::loadCard(const size_t i, FlashcardCard& out) const {
  if (i >= refs.size()) return false;
  const Ref& ref = refs[i];
  HalFile file;
  if (!Storage.openFileForRead(TAG, files[ref.file], file) || !file.seek(ref.offset)) {
    LOG_ERR(TAG, "Cannot read card %u", (unsigned)i);
    return false;
  }
  FlashcardCsvReader reader(file, ref.offset);
  FlashcardCsvReader::Record rec;
  if (!reader.next(rec)) return false;
  out.id = static_cast<uint16_t>(atoi(rec.id.c_str()));
  out.frontContent = std::string(rec.front);
  out.backContent = std::string(rec.back);
  out.srs = SrsState{};
  return true;
}
