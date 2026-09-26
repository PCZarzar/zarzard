#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardDeck.h"
#include "FlashcardLinearSession.h"
#include "FlashcardSrs.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Deck offered by "Next deck". Empty name = none.
struct FlashcardNextDeck {
  std::string name;
  uint16_t cards = 0;  // Spaced: due + new cards still allowed today
};

// Resolved when a session or linear pass ends, so the offer reflects the
// new-card quota the session just consumed.
struct FlashcardNextDeckProvider {
  void* ctx = nullptr;
  FlashcardNextDeck (*fn)(void* ctx) = nullptr;
  FlashcardNextDeck operator()() const { return fn ? fn(ctx) : FlashcardNextDeck{}; }
};

class FlashcardReviewActivity final : public Activity {
 public:
  enum class ReviewMode : uint8_t { Spaced = 0, Linear = 1 };

  // MenuResult::action set when the user asks to open the next deck.
  static constexpr int RESULT_NEXT_DECK = 1;

  // resumeLinear: continue the pass saved for this deck (Linear only); a fresh
  // Linear start discards any saved pass.
  FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& deckPath,
                          ReviewMode mode, bool shuffle, bool resumeLinear, FlashcardNextDeckProvider nextDeck);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class CardState { Front, Back, Summary };
  enum class SummaryOption { Repeat, Full, NextDeck, Exit };

  void flipCard();
  void rateCard(SrsRating rating);
  void saveProgress();
  void showDone();
  void markLinear(LinearPile pile);
  void loopLinear();
  void loopSummary();
  void buildSummaryOptions();
  void finishWithNextDeck();
  void saveLinearProgress();
  void renderCard(const FlashcardCard& card);
  void renderSummary();

  FlashcardDeck deck;
  std::string deckPath;
  ReviewMode mode;
  bool shuffle;
  bool resumeLinear;
  FlashcardNextDeckProvider nextDeckProvider;
  bool deckLoaded = false;

  // Spaced mode
  std::vector<size_t> reviewQueue;
  size_t currentQueueIndex = 0;
  uint32_t today = 0;
  size_t maxQueueSize = 0;                 // cap for re-queue growth (2x initial)
  uint16_t unsavedRatings = 0;             // ratings not yet written to the CSV
  bool pendingSave = false;                // batch save, flushed from loop()
  bool newQuotaDirty = false;              // daily new-card counter changed since last save
  std::array<uint16_t, 4> ratingCounts{};  // indexed by SrsRating

  CardState cardState = CardState::Front;

  // Linear mode only — RAM state, nothing is persisted.
  FlashcardLinearSession linear;
  std::string nextDeckName;  // resolved when a pass ends
  bool linearDirty = false;  // pass changed since the last progress save
  uint8_t marksSinceSave = 0;
  std::vector<SummaryOption> summaryOptions;
  int summarySelected = 0;
  ButtonNavigator buttonNavigator;
};
