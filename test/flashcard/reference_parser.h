#pragma once
// Reference: the in-memory CSV parser the firmware used up to ecec1a4d/dd3f88a8
// (whole file in one buffer). The streaming parser must produce exactly the
// same cards; test_flashcard.cpp compares both on generated CSVs.
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

struct RefCard {
  uint16_t id = 0;
  std::string front, back, due, interval, ease;
  bool operator==(const RefCard& o) const {
    return id == o.id && front == o.front && back == o.back && due == o.due && interval == o.interval && ease == o.ease;
  }
};

inline std::string refParseField(const char*& ptr) {
  std::string result;
  if (*ptr == '\0') return result;
  if (*ptr == '"') {
    ++ptr;
    while (*ptr != '\0') {
      if (*ptr == '"') {
        ++ptr;
        if (*ptr == '"') {
          result += '"';
          ++ptr;
        } else {
          break;
        }
      } else {
        result += *ptr++;
      }
    }
    if (*ptr == ',') ++ptr;
  } else {
    while (*ptr != '\0' && *ptr != ',' && *ptr != '\r' && *ptr != '\n') result += *ptr++;
    if (*ptr == ',') ++ptr;
  }
  return result;
}

// Returns the cards (up to maxCards) and whether the file had more.
inline std::vector<RefCard> refParse(std::string content, size_t maxCards, bool& truncated) {
  truncated = false;
  std::string clean;  // stray NUL bytes are dropped first
  for (char c : content)
    if (c != '\0') clean += c;
  const char* ptr = clean.c_str();
  while (*ptr != '\0' && *ptr != '\n') ++ptr;
  if (*ptr == '\n') ++ptr;
  std::vector<RefCard> cards;
  uint16_t nextId = 1;
  while (*ptr != '\0') {
    if (*ptr == '\r' || *ptr == '\n') {
      ++ptr;
      continue;
    }
    if (cards.size() >= maxCards) {
      truncated = true;
      break;
    }
    RefCard c;
    std::string idStr = refParseField(ptr);
    c.id = idStr.empty() ? nextId : (uint16_t)atoi(idStr.c_str());
    nextId = c.id + 1;
    c.front = refParseField(ptr);
    c.back = refParseField(ptr);
    c.due = refParseField(ptr);
    c.interval = refParseField(ptr);
    c.ease = refParseField(ptr);
    while (*ptr != '\0' && *ptr != '\n') ++ptr;
    if (*ptr == '\n') ++ptr;
    cards.push_back(std::move(c));
  }
  return cards;
}
