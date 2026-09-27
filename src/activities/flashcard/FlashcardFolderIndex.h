#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardDeck.h"

// Linear "whole folder": every card of a folder's decks in one pass without
// keeping their text in RAM. Only where each card starts in its CSV is kept
// (a few bytes per card); a card is read from the SD card when it is shown.
class FlashcardFolderIndex {
 public:
  static constexpr size_t MAX_CARDS = 3000;

  // Indexes the decks in order. False when nothing could be indexed (no
  // readable deck, no memory); decks that fail to read are skipped.
  bool build(const std::vector<std::string>& deckPaths);

  size_t size() const { return refs.size(); }
  bool isTruncated() const { return truncated; }  // MAX_CARDS reached or memory ran low

  // Hash of every indexed card's id, front and back, in order: tells a saved
  // pass whether the folder's cards changed.
  uint32_t fingerprint() const { return hash; }

  // Reads card i from its CSV (text only; SRS columns are not needed in Linear).
  bool loadCard(size_t i, FlashcardCard& out) const;

 private:
  struct Ref {
    uint32_t offset;  // where the record starts in its file
    uint16_t file;    // index into files
  };
  std::vector<std::string> files;
  std::vector<Ref> refs;
  uint32_t hash = 0;
  bool truncated = false;
};
