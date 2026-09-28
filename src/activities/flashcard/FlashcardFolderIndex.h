#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardCsv.h"
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

  // Chosen cards (e.g. the difficult ones), each given by its deck and record
  // position. Cards that no longer read back are skipped. The fingerprint
  // covers the positions and the text read, in order.
  struct CardRef {
    std::string path;
    uint32_t offset = 0;
  };
  bool buildFromRefs(const std::vector<CardRef>& cards);

  size_t size() const { return refs.size(); }
  bool isTruncated() const { return truncated; }  // MAX_CARDS reached or memory ran low

  // Hash of every indexed card's id, front and back, in order: tells a saved
  // pass whether the folder's cards changed.
  uint32_t fingerprint() const { return hash; }

  // Reads card i from its CSV (text and flags; SRS columns are not needed in Linear).
  bool loadCard(size_t i, FlashcardCard& out) const;

  // Deck file and record position of card i (to change it in its CSV later).
  bool refOf(size_t i, std::string& path, uint32_t& offset) const;
  // Card flags as indexed (suspended cards are never indexed), changeable in RAM.
  uint8_t flagsOf(size_t i) const { return i < refs.size() ? refs[i].flags : 0; }
  void setFlags(size_t i, uint8_t flags) {
    if (i < refs.size()) refs[i].flags = flags;
  }

 private:
  struct Ref {
    uint32_t offset;  // where the record starts in its file
    uint16_t file;    // index into files
    uint8_t flags;    // FlashcardCard::FLAG_* (fits in the struct's padding)
  };
  // Reads card i's record as it is in its CSV.
  bool readRecord(size_t i, FlashcardCsvReader::Record& rec) const;

  std::vector<std::string> files;
  std::vector<Ref> refs;
  uint32_t hash = 0;
  bool truncated = false;
};
