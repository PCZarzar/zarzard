#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardLinearSession.h"

// Linear review progress saved per deck in /.crosspoint/fc_linear_<hash>.json,
// so leaving a pass (Back, sleep, power button) can be continued later. The
// deck's CSV is never touched.
namespace FlashcardLinearProgress {

struct Saved {
  uint32_t fingerprint = 0;  // FlashcardDeck::contentFingerprint() when saved
  int8_t shuffle = -1;       // order of the pass: 1 shuffled, 0 in order, -1 not saved (older files)
  FlashcardLinearSession::State state;
};

bool load(const std::string& deckPath, Saved& out);
bool save(const std::string& deckPath, const Saved& saved);
void clear(const std::string& deckPath);

// A folder's "Selection": decks chosen to be studied as one pass, in list
// order (one selection per folder). Its pass is saved under selectionKey().
// folderDir is the folder path without a trailing slash, e.g. "/flashcard/LPIC 101".
std::string selectionKey(const std::string& folderDir);
// True for a selectionKey(); folderDirOut gets the folder.
bool isSelectionKey(const std::string& key, std::string& folderDirOut);
bool saveSelection(const std::string& folderDir, const std::vector<std::string>& deckPaths);
// The saved decks that still exist, in their saved order (empty when none).
std::vector<std::string> loadSelection(const std::string& folderDir);
// Forgets the folder's Selection and its saved pass; the decks are untouched.
void clearSelection(const std::string& folderDir);

}  // namespace FlashcardLinearProgress
