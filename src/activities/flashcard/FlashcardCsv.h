#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "FlashcardSrs.h"

class HalFile;

// Streaming reader for flashcard CSVs: reads the file through a small buffer
// instead of loading it whole. Field and record rules are exactly those of the
// original in-memory parser (quoted fields with "" escapes, blank lines
// skipped, missing trailing fields empty, extra fields ignored, stray NUL
// bytes dropped); host tests compare both on generated files.
class FlashcardCsvReader {
 public:
  static constexpr size_t MAX_FIELD_BYTES = 4096;  // longer fields are cut (sanity limit)

  struct Record {
    std::string id, front, back, due, interval, ease, flags;
    uint32_t offset = 0;  // file position where the record starts

    // SRS columns: all three present (ease clamped to 130..400), else a new card.
    SrsState srs() const;
    // Optional seventh column: 'm' marked, 's' suspended (FlashcardCard::FLAG_*).
    uint8_t flagBits() const;
  };

  // Reads `file` from its current position; `startOffset` is that position.
  explicit FlashcardCsvReader(HalFile& file, uint32_t startOffset = 0);

  // Skips the header line.
  void skipHeader();
  // Next record; false at end of file (or on a read error, see failed()).
  bool next(Record& rec);

  bool failed() const { return error; }             // read error or no buffer
  uint32_t bytesRead() const { return totalRead; }  // raw bytes read from the file

 private:
  char peek();
  char get();
  void readField(std::string& out);
  bool fill();

  static constexpr size_t BUFFER_SIZE = 512;
  HalFile& file;
  std::unique_ptr<char[]> buf;
  size_t pos = 0;
  size_t len = 0;
  uint32_t bufOffset;  // file position of buf[0]
  uint32_t totalRead = 0;
  bool eof = false;
  bool error = false;
};
