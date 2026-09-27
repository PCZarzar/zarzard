#include "FlashcardPlanner.h"

#include <algorithm>

namespace FlashcardPlanner {

namespace {
uint32_t xorshift(uint32_t& s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
}  // namespace

FlashcardDeckState defaultState(const DeckStats& stats) {
  return stats.totalCards > stats.newCards ? FlashcardDeckState::Active : FlashcardDeckState::StandBy;
}

FlashcardDeckState nextState(const FlashcardDeckState s) {
  switch (s) {
    case FlashcardDeckState::Active:
      return FlashcardDeckState::ReviewsOnly;
    case FlashcardDeckState::ReviewsOnly:
      return FlashcardDeckState::StandBy;
    default:
      return FlashcardDeckState::Active;
  }
}

bool eligible(const PlanDeck& d) { return d.state == FlashcardDeckState::Active && d.spacedOk && d.newCards > 0; }

std::vector<Allot> split(const std::vector<PlanDeck>& decks, const std::vector<Allot>& previous, const uint16_t total,
                         const uint16_t usedToday, const uint32_t seed) {
  const auto usedBefore = [&previous](const std::string& path) -> uint16_t {
    for (const auto& p : previous) {
      if (p.path == path) return p.used;
    }
    return 0;
  };

  uint32_t usedSum = 0;
  for (const auto& p : previous) usedSum += p.used;
  const uint32_t used = std::max<uint32_t>(usedSum, usedToday);
  uint32_t left = total > used ? total - used : 0;

  struct Slot {
    size_t out;
    uint32_t share;
    uint16_t cap;  // new cards left in the deck
    uint32_t tie;
  };
  std::vector<Allot> out;
  out.reserve(decks.size() + previous.size());
  std::vector<Slot> slots;
  slots.reserve(decks.size());
  uint32_t rng = seed ? seed : 0x9E3779B9u;

  for (const auto& d : decks) {
    const uint16_t u = usedBefore(d.path);
    if (!eligible(d) && u == 0) continue;
    out.push_back(Allot{d.path, u, u});
    if (eligible(d)) {
      slots.push_back(Slot{out.size() - 1, std::max<uint32_t>(d.share, SHARE_MIN), d.newCards, xorshift(rng)});
    }
  }
  // Decks gone from the list (renamed, removed, over the limit) keep what they used.
  for (const auto& p : previous) {
    if (p.used == 0) continue;
    const bool listed = std::any_of(out.begin(), out.end(), [&p](const Allot& a) { return a.path == p.path; });
    if (!listed) out.push_back(Allot{p.path, p.used, p.used});
  }

  // Sainte-Laguë: each card goes to the deck with the highest share / (2 * allot + 1).
  while (left > 0) {
    int best = -1;
    for (size_t i = 0; i < slots.size(); ++i) {
      const Slot& s = slots[i];
      const Allot& a = out[s.out];
      if (a.allot - a.used >= s.cap) continue;
      if (best < 0) {
        best = static_cast<int>(i);
        continue;
      }
      const Slot& b = slots[best];
      const uint64_t lhs = static_cast<uint64_t>(s.share) * (2u * out[b.out].allot + 1u);
      const uint64_t rhs = static_cast<uint64_t>(b.share) * (2u * a.allot + 1u);
      if (lhs > rhs || (lhs == rhs && s.tie > b.tie)) best = static_cast<int>(i);
    }
    if (best < 0) break;  // every eligible deck is out of new cards
    ++out[slots[best].out].allot;
    --left;
  }
  return out;
}

uint16_t remaining(const std::vector<Allot>& allots, const std::string& path) {
  for (const auto& a : allots) {
    if (a.path == path) return a.allot > a.used ? a.allot - a.used : 0;
  }
  return 0;
}

uint32_t shareSum(const std::vector<PlanDeck>& decks) {
  uint32_t sum = 0;
  for (const auto& d : decks) {
    if (eligible(d)) sum += std::max<uint32_t>(d.share, SHARE_MIN);
  }
  return sum;
}

}  // namespace FlashcardPlanner
