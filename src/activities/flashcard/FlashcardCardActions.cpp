#include "FlashcardCardActions.h"

#include <HalStorage.h>
#include <Logging.h>

#include "FlashcardCsv.h"
#include "FlashcardStatsScan.h"

namespace FlashcardCardActions {

namespace {
constexpr const char* TAG = "FCACT";
}  // namespace

void apply(FlashcardCard& card, const FlashcardCardAction action) {
  switch (action) {
    case FlashcardCardAction::Mark:
      card.flags |= FlashcardCard::FLAG_MARKED;
      break;
    case FlashcardCardAction::Unmark:
      card.flags &= static_cast<uint8_t>(~FlashcardCard::FLAG_MARKED);
      break;
    case FlashcardCardAction::Suspend:
      card.flags |= FlashcardCard::FLAG_SUSPENDED;
      break;
    case FlashcardCardAction::Unsuspend:
      card.flags &= static_cast<uint8_t>(~FlashcardCard::FLAG_SUSPENDED);
      break;
    case FlashcardCardAction::Reset:
      card.srs = SrsState{};
      break;
    case FlashcardCardAction::Delete:
      card.flags |= FlashcardCard::FLAG_DELETED;
      break;
  }
}

namespace {
// One deck's share of applyToFiles: the actions on it, in order.
struct DeckEdits {
  const std::vector<Pending>* actions;
  std::vector<size_t> mine;  // indices into actions
  size_t applied = 0;
};

void editByOffset(FlashcardCard& card, void* ctx) {
  auto& e = *static_cast<DeckEdits*>(ctx);
  for (const size_t i : e.mine) {
    const Pending& p = (*e.actions)[i];
    if (p.offset != card.offset || (card.flags & FlashcardCard::FLAG_DELETED)) continue;
    apply(card, p.action);
    ++e.applied;
  }
}

struct FlagEdit {
  uint8_t mask;
  FlashcardCardAction action;
  size_t changed = 0;
};

void editByFlag(FlashcardCard& card, void* ctx) {
  auto& e = *static_cast<FlagEdit*>(ctx);
  if (!(card.flags & e.mask)) return;
  apply(card, e.action);
  ++e.changed;
}
}  // namespace

size_t applyToFiles(const std::vector<Pending>& actions) {
  size_t applied = 0;
  std::vector<bool> done(actions.size(), false);
  for (size_t first = 0; first < actions.size(); ++first) {
    if (done[first]) continue;
    const std::string& path = actions[first].path;
    DeckEdits edits{&actions, {}, 0};
    for (size_t i = first; i < actions.size(); ++i) {
      if (done[i] || actions[i].path != path) continue;
      done[i] = true;
      edits.mine.push_back(i);
    }
    if (!FlashcardDeck::rewriteCsv(path.c_str(), editByOffset, &edits)) {
      LOG_ERR(TAG, "Cannot change %s", path.c_str());
      continue;
    }
    if (edits.applied < edits.mine.size()) {
      LOG_ERR(TAG, "%u action(s) found no card in %s", (unsigned)(edits.mine.size() - edits.applied), path.c_str());
    }
    applied += edits.applied;
  }
  return applied;
}

size_t applyToFlagged(const std::vector<std::string>& deckPaths, const uint8_t flagMask,
                      const FlashcardCardAction action, size_t* failed) {
  size_t changed = 0;
  if (failed) *failed = 0;
  for (const auto& path : deckPaths) {
    FlagEdit edit{flagMask, action, 0};
    if (!FlashcardDeck::rewriteCsv(path.c_str(), editByFlag, &edit)) {
      LOG_ERR(TAG, "Cannot change %s", path.c_str());
      if (failed) ++*failed;
      continue;
    }
    changed += edit.changed;
  }
  return changed;
}

std::vector<FlaggedCount> countFlagged(const std::vector<std::string>& deckPaths) {
  std::vector<FlaggedCount> out;
  FlashcardCsvReader::Record rec;
  for (const auto& path : deckPaths) {
    HalFile file;
    if (!Storage.openFileForRead(TAG, path, file)) continue;
    FlashcardCsvReader reader(file);
    reader.skipHeader();
    FlaggedCount c;
    c.path = path;
    while (reader.next(rec)) {
      const uint8_t flags = rec.flagBits();
      if (flags & FlashcardCard::FLAG_MARKED) ++c.marked;
      if (flags & FlashcardCard::FLAG_SUSPENDED) ++c.suspended;
    }
    if (c.marked > 0 || c.suspended > 0) out.push_back(std::move(c));
  }
  return out;
}

std::vector<Flagged> collectFlagged(const std::vector<std::string>& deckPaths, const size_t max) {
  std::vector<Flagged> out;
  FlashcardCsvReader::Record rec;
  for (const auto& path : deckPaths) {
    if (out.size() >= max) break;
    HalFile file;
    if (!Storage.openFileForRead(TAG, path, file)) continue;
    FlashcardCsvReader reader(file);
    reader.skipHeader();
    const std::string deckName = FlashcardDeck::displayNameForPath(path);
    size_t cards = 0;
    while (out.size() < max && cards < MAX_CARDS_PER_DECK && reader.next(rec)) {
      ++cards;
      const uint8_t flags = rec.flagBits();
      if (!(flags & (FlashcardCard::FLAG_MARKED | FlashcardCard::FLAG_SUSPENDED))) continue;
      Flagged f;
      f.path = path;
      f.deckName = deckName;
      FlashcardCard card;
      card.frontContent = rec.front;
      f.front = FlashcardStatsScan::shortText(card.frontMain());
      f.back = FlashcardStatsScan::shortText(FlashcardDeck::decodeNewlines(rec.back));
      f.offset = rec.offset;
      f.flags = flags;
      out.push_back(std::move(f));
    }
  }
  return out;
}

}  // namespace FlashcardCardActions
