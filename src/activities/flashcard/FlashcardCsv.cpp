#include "FlashcardCsv.h"

#include <HalStorage.h>
#include <Memory.h>

#include <cstdlib>

FlashcardCsvReader::FlashcardCsvReader(HalFile& file, const uint32_t startOffset)
    : file(file), buf(makeUniqueNoThrow<char[]>(BUFFER_SIZE)), bufOffset(startOffset) {
  if (!buf) {
    error = true;
    eof = true;
  }
}

bool FlashcardCsvReader::fill() {
  if (eof) return false;
  bufOffset += static_cast<uint32_t>(len);
  pos = 0;
  len = 0;
  const int n = file.read(buf.get(), BUFFER_SIZE);
  if (n < 0) {
    error = true;
    eof = true;
    return false;
  }
  if (n == 0) {
    eof = true;
    return false;
  }
  len = static_cast<size_t>(n);
  totalRead += static_cast<uint32_t>(n);
  return true;
}

// '\0' means end of input; NUL bytes in the file are skipped (older firmware
// wrote one after the header).
char FlashcardCsvReader::peek() {
  while (true) {
    if (pos < len) {
      if (buf[pos] != '\0') return buf[pos];
      ++pos;
      continue;
    }
    if (!fill()) return '\0';
  }
}

char FlashcardCsvReader::get() {
  const char c = peek();
  if (c != '\0') ++pos;
  return c;
}

void FlashcardCsvReader::readField(std::string& out) {
  out.clear();
  const auto append = [&out](char c) {
    if (out.size() < MAX_FIELD_BYTES) out += c;
  };
  char c = peek();
  if (c == '\0') return;
  if (c == '"') {
    get();
    while ((c = peek()) != '\0') {
      if (c == '"') {
        get();
        if (peek() == '"') {
          append('"');
          get();
        } else {
          break;
        }
      } else {
        append(get());
      }
    }
    if (peek() == ',') get();
  } else {
    while ((c = peek()) != '\0' && c != ',' && c != '\r' && c != '\n') append(get());
    if (peek() == ',') get();
  }
}

void FlashcardCsvReader::skipHeader() {
  char c;
  while ((c = peek()) != '\0' && c != '\n') get();
  if (peek() == '\n') get();
}

bool FlashcardCsvReader::next(Record& rec) {
  char c;
  while ((c = peek()) == '\r' || c == '\n') get();  // blank lines
  if (c == '\0') return false;
  rec.offset = bufOffset + static_cast<uint32_t>(pos);
  readField(rec.id);
  readField(rec.front);
  readField(rec.back);
  readField(rec.due);
  readField(rec.interval);
  readField(rec.ease);
  while ((c = peek()) != '\0' && c != '\n') get();  // anything after the sixth field
  if (peek() == '\n') get();
  return true;
}

SrsState FlashcardCsvReader::Record::srs() const {
  SrsState out;
  if (!due.empty() && !interval.empty() && !ease.empty()) {
    out.dueDate = FlashcardSrs::parseDateStr(due.c_str());
    out.interval = (uint16_t)atoi(interval.c_str());
    out.ease = (uint16_t)atoi(ease.c_str());
    if (out.ease < 130) out.ease = 130;
    if (out.ease > 400) out.ease = 400;
  }
  return out;
}
