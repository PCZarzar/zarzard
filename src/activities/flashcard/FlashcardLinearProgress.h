#pragma once
#include <cstdint>
#include <string>

#include "FlashcardLinearSession.h"

// Linear review progress saved per deck in /.crosspoint/fc_linear_<hash>.json,
// so leaving a pass (Back, sleep, power button) can be continued later. The
// deck's CSV is never touched.
namespace FlashcardLinearProgress {

struct Saved {
  uint32_t fingerprint = 0;  // FlashcardDeck::contentFingerprint() when saved
  FlashcardLinearSession::State state;
};

bool load(const std::string& deckPath, Saved& out);
bool save(const std::string& deckPath, const Saved& saved);
void clear(const std::string& deckPath);

}  // namespace FlashcardLinearProgress
