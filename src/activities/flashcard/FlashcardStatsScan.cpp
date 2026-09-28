#include "FlashcardStatsScan.h"

#include <HalStorage.h>

#include <algorithm>

#include "FlashcardCsv.h"
#include "FlashcardHistory.h"

namespace FlashcardStatsScan {

namespace {
constexpr const char* TAG = "FCSTAT";
}  // namespace

bool harder(const FlashcardHardCard& a, const FlashcardHardCard& b) {
  if (a.ease != b.ease) return a.ease < b.ease;
  if (a.interval != b.interval) return a.interval < b.interval;
  if (a.path != b.path) return a.path < b.path;
  return a.offset < b.offset;
}

std::string shortText(const std::string& s) {
  std::string out;
  out.reserve(std::min(s.size(), TEXT_BYTES));
  for (const char c : s) {
    if (out.size() >= TEXT_BYTES) break;
    out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
  }
  if (out.size() >= TEXT_BYTES && s.size() > out.size()) {
    // Do not leave half of a UTF-8 character at the end.
    size_t cut = out.size();
    while (cut > 0 && (static_cast<unsigned char>(out[cut - 1]) & 0xC0) == 0x80) --cut;
    if (cut > 0 && (static_cast<unsigned char>(out[cut - 1]) & 0x80)) --cut;
    out.resize(cut);
  }
  return out;
}

bool scanDeck(const std::string& path, const uint32_t today, DeckResult& out, uint32_t* forecast,
              std::vector<FlashcardHardCard>* hard) {
  out = DeckResult{};
  HalFile file;
  if (!Storage.openFileForRead(TAG, path, file)) return false;
  const size_t fsize = file.size();
  if (fsize == 0) return false;
  FlashcardCsvReader reader(file);
  FlashcardCsvReader::Record rec;
  reader.skipHeader();
  const std::string deckName = FlashcardDeck::displayNameForPath(path);
  while (reader.next(rec)) {
    if (out.stats.totalCards >= MAX_CARDS_PER_DECK) {
      out.truncated = true;
      break;
    }
    ++out.stats.totalCards;
    const uint8_t flags = rec.flagBits();
    if (flags & FlashcardCard::FLAG_MARKED) ++out.stats.markedCards;
    if (flags & FlashcardCard::FLAG_SUSPENDED) {
      ++out.stats.suspendedCards;  // out of reviews: no phase, forecast or difficulty
      continue;
    }
    const SrsState srs = rec.srs();
    if (srs.dueDate == 0) {
      ++out.stats.newCards;
      ++out.phases.newCards;
      continue;
    }
    if (srs.dueDate <= today) ++out.stats.dueCards;
    if (srs.interval >= FlashcardHistory::MATURE_INTERVAL) {
      ++out.phases.mature;
    } else {
      ++out.phases.learning;
    }
    if (forecast && today != 0) {
      const uint32_t in = srs.dueDate <= today ? 0 : srs.dueDate - today;
      if (in < static_cast<uint32_t>(FORECAST_DAYS)) ++forecast[in];
    }
    if (hard && srs.ease <= HARD_EASE) {
      FlashcardHardCard c;
      c.path = path;
      c.offset = rec.offset;
      c.ease = srs.ease;
      c.interval = srs.interval;
      if (hard->size() >= MAX_HARD && !harder(c, hard->back())) continue;  // not among the worst
      FlashcardCard card;
      card.frontContent = rec.front;
      c.front = shortText(card.frontMain());
      c.back = shortText(FlashcardDeck::decodeNewlines(rec.back));
      c.deckName = deckName;
      hard->insert(std::upper_bound(hard->begin(), hard->end(), c, harder), std::move(c));
      if (hard->size() > MAX_HARD) hard->pop_back();
    }
  }
  if (reader.failed()) return false;
  if (!out.truncated && reader.bytesRead() != fsize) return false;
  return true;
}

std::vector<FlashcardHardCard> collectHard(const std::vector<std::string>& deckPaths) {
  std::vector<FlashcardHardCard> hard;
  hard.reserve(MAX_HARD + 1);
  DeckResult r;
  for (const auto& path : deckPaths) scanDeck(path, 0, r, nullptr, &hard);
  return hard;
}

}  // namespace FlashcardStatsScan
