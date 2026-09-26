#pragma once
#include <cctype>
#include <cstring>
#include <string>

// Summaries live in their own folder, browsed from the Summaries Home entry
// and kept out of the book library (hidden in Browse files, never in Recents).
constexpr char SUMMARIES_DIR[] = "/summaries";

// True for files inside SUMMARIES_DIR. Case-insensitive, like the FAT card.
inline bool isSummaryPath(const std::string& path) {
  constexpr size_t n = sizeof(SUMMARIES_DIR) - 1;
  if (path.size() <= n || path[n] != '/') return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(path[i])) != SUMMARIES_DIR[i]) return false;
  }
  return true;
}

// True for the "summaries" entry name in the card root (case-insensitive).
inline bool isSummariesDirName(const char* name) {
  constexpr size_t n = sizeof(SUMMARIES_DIR) - 2;  // without the leading '/'
  if (std::strlen(name) != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(name[i])) != SUMMARIES_DIR[i + 1]) return false;
  }
  return true;
}
