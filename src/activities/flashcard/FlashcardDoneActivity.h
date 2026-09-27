#pragma once
#include <array>
#include <cstdint>

#include "FlashcardReviewActivity.h"
#include "activities/Activity.h"

// End of a Spaced session: rating counts and the next deck of the round.
// Confirm → MenuResult{FlashcardReviewActivity::RESULT_NEXT_DECK}; Back → list.
class FlashcardDoneActivity final : public Activity {
 public:
  FlashcardDoneActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        const std::array<uint16_t, 4>& ratingCounts, const FlashcardNextDeck& next,
                        const char* message = nullptr)
      : Activity("FlashcardDone", renderer, mappedInput), ratingCounts(ratingCounts), next(next), message(message) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::array<uint16_t, 4> ratingCounts;
  FlashcardNextDeck next;
  const char* message;  // replaces "Session complete" (e.g. the deck could not be read); nullptr = none
};
