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

void describe(const std::string& path, Entry& out) {
  out.path = path;
  out.name = FlashcardDeck::displayNameForPath(path);
  const std::string prefix = std::string(FLASHCARD_DIR) + "/";
  const size_t slash = path.rfind('/');
  out.folder = (slash != std::string::npos && slash > prefix.size() - 1)
                   ? path.substr(prefix.size(), slash - prefix.size())
                   : std::string();
}

bool load(const std::string& path, const uint32_t today, Entry& out) {
  // Counts only: no card is kept in RAM, whatever the deck size.
  if (!FlashcardDeck::scanStats(path.c_str(), today, out.stats, out.truncated)) return false;
  describe(path, out);
  return true;
}

std::vector<FlashcardPlanner::PlanDeck> planDecks(const std::vector<Entry>& entries) {
  std::vector<FlashcardPlanner::PlanDeck> out;
  out.reserve(entries.size());
  for (const auto& e : entries) {
    FlashcardPlanner::PlanDeck d;
    d.path = e.path;
    d.state = e.state;
    d.share = e.share;
    d.newCards = e.stats.newCards;
    d.spacedOk = !e.truncated;
    out.push_back(std::move(d));
  }
  return out;
}

bool applyPlan(std::vector<Entry>& entries, FlashcardDeckOptions& options, const uint32_t today, const uint16_t total,
               const uint16_t usedToday, const uint32_t seed) {
  for (auto& e : entries) {
    e.state = options.stateFor(e.path, e.stats);
    e.share = options.shareFor(e.path);
  }
  const bool changed = options.refreshSplit(planDecks(entries), today, total, usedToday, seed);
  for (auto& e : entries) e.newToday = options.newRemaining(e.path, today);
  return changed;
}

bool isPending(const Entry& e) {
  if (e.truncated || e.state == FlashcardDeckState::StandBy) return false;
  return e.stats.dueCards > 0 || (e.stats.newCards > 0 && e.newToday > 0);
}

void sortForSpaced(std::vector<Entry>& entries) {
  std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    const bool pa = isPending(a);
    const bool pb = isPending(b);
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
