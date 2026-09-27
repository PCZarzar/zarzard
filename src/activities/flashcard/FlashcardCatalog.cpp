#include "FlashcardCatalog.h"

#include <algorithm>

namespace FlashcardCatalog {

std::vector<std::string> decksIn(const std::string& folder) {
  return FlashcardDeck::listDecks(folder.empty() ? std::string(FLASHCARD_DIR)
                                                 : std::string(FLASHCARD_DIR) + "/" + folder);
}

std::vector<std::string> allDecks() {
  std::vector<std::string> paths = decksIn("");
  for (const auto& folder : FlashcardDeck::listFolders()) {
    auto inFolder = decksIn(folder);
    paths.insert(paths.end(), inFolder.begin(), inFolder.end());
  }
  return paths;
}

bool load(const std::string& path, const uint32_t today, Entry& out) {
  // Counts only: no card is kept in RAM, whatever the deck size.
  if (!FlashcardDeck::scanStats(path.c_str(), today, out.stats, out.truncated)) return false;
  out.path = path;
  out.name = FlashcardDeck::displayNameForPath(path);
  const std::string prefix = std::string(FLASHCARD_DIR) + "/";
  const size_t slash = path.rfind('/');
  out.folder = (slash != std::string::npos && slash > prefix.size() - 1)
                   ? path.substr(prefix.size(), slash - prefix.size())
                   : std::string();
  return true;
}

bool isPending(const Entry& e, const uint16_t newQuota) {
  if (e.truncated) return false;
  return e.stats.dueCards > 0 || (e.stats.newCards > 0 && newQuota > 0);
}

void sortForSpaced(std::vector<Entry>& entries, const uint16_t newQuota) {
  std::stable_sort(entries.begin(), entries.end(), [newQuota](const Entry& a, const Entry& b) {
    const bool pa = isPending(a, newQuota);
    const bool pb = isPending(b, newQuota);
    if (pa != pb) return pa;
    if (pa) {
      if (a.stats.dueCards != b.stats.dueCards) return a.stats.dueCards > b.stats.dueCards;
      if (a.stats.newCards != b.stats.newCards) return a.stats.newCards > b.stats.newCards;
    }
    if (a.name != b.name) return a.name < b.name;
    return a.path < b.path;
  });
}

}  // namespace FlashcardCatalog
