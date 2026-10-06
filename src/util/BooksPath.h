#pragma once
#include <cctype>
#include <string>

// The Books Home entry browses this folder only (created on first use).
constexpr char BOOKS_DIR[] = "/books";

// True for BOOKS_DIR itself and files inside it. Case-insensitive, like the FAT card.
inline bool isBooksPath(const std::string& path) {
  constexpr size_t n = sizeof(BOOKS_DIR) - 1;
  if (path.size() < n || (path.size() > n && path[n] != '/')) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(path[i])) != BOOKS_DIR[i]) return false;
  }
  return true;
}
