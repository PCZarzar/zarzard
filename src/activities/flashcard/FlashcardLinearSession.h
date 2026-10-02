#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

// State of a Linear review: the current pass queue and the three piles the
// user sorts cards into. Never touches SRS state or the CSV; it can be
// exported and restored (FlashcardLinearProgress keeps it on the SD card).
// Kept free of device dependencies so it can be tested on the host.
enum class LinearPile : uint8_t { Missed, Unsure, Known };

class FlashcardLinearSession {
 public:
  // Everything needed to continue a pass later.
  struct State {
    uint16_t pass = 0;
    size_t position = 0;  // cards already sorted in this pass
    std::vector<size_t> queue;
    std::vector<size_t> missed;
    std::vector<size_t> unsure;
    std::vector<size_t> known;
  };

  State exportState() const { return State{passNumber, position, queue, missed, unsure, known}; }

  // Restores a saved pass for a deck with deckSize cards. Rejects anything
  // inconsistent (deck changed, corrupted file): indices out of range or
  // repeated, position past the queue, piles that are not exactly the cards
  // already sorted. On failure the session is left untouched.
  bool restore(const State& st, size_t deckSize, bool shuffleEachPass) {
    if (st.pass == 0 || st.position > st.queue.size() || st.queue.size() > deckSize) return false;
    std::vector<size_t> q = st.queue;
    std::sort(q.begin(), q.end());
    if (std::adjacent_find(q.begin(), q.end()) != q.end()) return false;
    if (!q.empty() && q.back() >= deckSize) return false;
    std::vector<size_t> sorted(st.queue.begin(), st.queue.begin() + static_cast<std::ptrdiff_t>(st.position));
    std::vector<size_t> piles = st.missed;
    piles.insert(piles.end(), st.unsure.begin(), st.unsure.end());
    piles.insert(piles.end(), st.known.begin(), st.known.end());
    std::sort(sorted.begin(), sorted.end());
    std::sort(piles.begin(), piles.end());
    if (sorted != piles) return false;

    shuffle = shuffleEachPass;
    this->deckSize = deckSize;
    passNumber = st.pass;
    position = st.position;
    queue = st.queue;
    missed = st.missed;
    unsure = st.unsure;
    known = st.known;
    return true;
  }

  // First pass over every card of a deck with deckSize cards.
  void start(size_t deckSize, bool shuffleEachPass, uint32_t seed) {
    shuffle = shuffleEachPass;
    passNumber = 0;
    this->deckSize = deckSize;
    startFullPass(seed);
  }

  void startFullPass(uint32_t seed) {
    std::vector<size_t> all(deckSize);
    for (size_t i = 0; i < deckSize; ++i) all[i] = i;
    beginPass(std::move(all), seed);
  }

  // New pass with only the Missed and Unsure cards of the pass just ended.
  void startRepeatPass(uint32_t seed) {
    std::vector<size_t> next = missed;
    next.insert(next.end(), unsure.begin(), unsure.end());
    std::sort(next.begin(), next.end());  // CSV order unless shuffled
    beginPass(std::move(next), seed);
  }

  bool canRepeat() const { return !missed.empty() || !unsure.empty(); }

  // Takes a card out of this session (suspended or deleted): it leaves the
  // rest of the current pass, the piles and every later pass. The card on
  // screen can be removed; so can one already sorted in this pass.
  void exclude(size_t idx) {
    if (std::find(excluded.begin(), excluded.end(), idx) != excluded.end()) return;
    excluded.push_back(idx);
    for (size_t i = position; i < queue.size();) {
      if (queue[i] == idx) {
        queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
    for (std::vector<size_t>* pile : {&missed, &unsure, &known}) {
      const auto it = std::find(pile->begin(), pile->end(), idx);
      if (it == pile->end()) continue;
      pile->erase(it);
      // It was sorted earlier in this pass: drop it from the sorted part too.
      for (size_t i = 0; i < position; ++i) {
        if (queue[i] == idx) {
          queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(i));
          --position;
          break;
        }
      }
    }
  }
  bool isExcluded(size_t idx) const { return std::find(excluded.begin(), excluded.end(), idx) != excluded.end(); }
  size_t excludedCount() const { return excluded.size(); }

  // Adapts a saved pass to the deck after the cards at `removed` (indices in
  // the old deck) left it for good: the remaining cards keep their order, so
  // each index moves down by the removed cards before it. False when a removed
  // card is still part of the pass (it should have been excluded first).
  static bool dropCards(State& st, std::vector<size_t> removed) {
    std::sort(removed.begin(), removed.end());
    removed.erase(std::unique(removed.begin(), removed.end()), removed.end());
    const auto fix = [&removed](std::vector<size_t>& v) {
      for (size_t& i : v) {
        const auto it = std::lower_bound(removed.begin(), removed.end(), i);
        if (it != removed.end() && *it == i) return false;
        i -= static_cast<size_t>(it - removed.begin());
      }
      return true;
    };
    return fix(st.queue) && fix(st.missed) && fix(st.unsure) && fix(st.known);
  }

  bool passDone() const { return position >= queue.size(); }
  size_t current() const { return queue[position]; }

  void mark(LinearPile pile) {
    if (passDone()) return;
    const size_t idx = queue[position++];
    switch (pile) {
      case LinearPile::Missed:
        missed.push_back(idx);
        break;
      case LinearPile::Unsure:
        unsure.push_back(idx);
        break;
      case LinearPile::Known:
        known.push_back(idx);
        break;
    }
  }

  bool shuffled() const { return shuffle; }
  uint16_t pass() const { return passNumber; }
  size_t passSize() const { return queue.size(); }
  // 1-based index of the card on screen (== passSize() once done).
  size_t shownPosition() const { return std::min(position + 1, queue.size()); }
  const std::vector<size_t>& passQueue() const { return queue; }
  size_t missedCount() const { return missed.size(); }
  size_t unsureCount() const { return unsure.size(); }
  size_t knownCount() const { return known.size(); }

 private:
  void beginPass(std::vector<size_t>&& cards, uint32_t seed) {
    queue = std::move(cards);
    queue.erase(std::remove_if(queue.begin(), queue.end(), [this](size_t i) { return isExcluded(i); }), queue.end());
    if (shuffle) {
      std::minstd_rand rng(seed ? seed : 1);
      std::shuffle(queue.begin(), queue.end(), rng);
    }
    position = 0;
    ++passNumber;
    missed.clear();
    unsure.clear();
    known.clear();
  }

  bool shuffle = true;
  size_t deckSize = 0;
  uint16_t passNumber = 0;
  size_t position = 0;
  std::vector<size_t> queue;
  std::vector<size_t> missed;
  std::vector<size_t> unsure;
  std::vector<size_t> known;
  std::vector<size_t> excluded;  // taken out this session; not saved (the deck changed anyway)
};
