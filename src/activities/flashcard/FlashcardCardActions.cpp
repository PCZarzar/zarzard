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

size_t applyToFiles(const std::vector<Pending>& actions) {
  size_t applied = 0;
  std::vector<bool> done(actions.size(), false);
  for (size_t first = 0; first < actions.size(); ++first) {
    if (done[first]) continue;
    const std::string& path = actions[first].path;
    FlashcardDeck deck;
    const bool loaded = deck.loadFromCsv(path.c_str()) && !deck.isTruncated();
    size_t here = 0;
    for (size_t i = first; i < actions.size(); ++i) {
      if (done[i] || actions[i].path != path) continue;
      done[i] = true;
      if (!loaded) continue;
      const int idx = deck.findByOffset(actions[i].offset);
      if (idx < 0) {
        LOG_ERR(TAG, "Card at %lu not found in %s", static_cast<unsigned long>(actions[i].offset), path.c_str());
        continue;
      }
      apply(deck.getCards()[idx], actions[i].action);
      ++here;
    }
    if (!loaded) {
      LOG_ERR(TAG, "Cannot change %s (unreadable or over %u cards)", path.c_str(), (unsigned)MAX_CARDS_PER_DECK);
      continue;
    }
    if (here > 0 && deck.saveToCsv(path.c_str())) applied += here;
  }
  return applied;
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
