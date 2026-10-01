#pragma once
#include <string>
#include <vector>

#include "FlashcardReviewActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Linear review: the difficult cards of every deck, then folders of /flashcard
// (one level) and their decks. Inside a folder the first row studies the whole
// folder as one fully shuffled pass.
// Decks (and whole folders) with a saved pass offer Continue / Restart.
// "Next deck" at the end of a deck's pass moves to the next deck of the folder.
class FlashcardLinearBrowserActivity final : public Activity {
  struct Item {
    bool isFolder = false;
    bool isWholeFolder = false;  // every deck of the current folder, shuffled
    bool isHard = false;         // the difficult cards of every deck
    uint32_t folderCards = 0;    // whole folder: cards in all its decks
    std::string name;            // folder name or deck display name
    std::string path;            // deck path
    uint16_t cards = 0;
    uint16_t folderDecks = 0;
    bool loaded = true;      // deck CSV could be read
    std::string savedLabel;  // "Pass N · i/n" when a pass is saved
  };

  ButtonNavigator buttonNavigator;
  std::string folder;  // "" = /flashcard itself
  std::vector<Item> items;
  int selectedIndex = 0;
  bool choosingStart = false;  // Continue / Restart for the selected deck
  int startChoice = 0;         // 0 = Continue, 1 = Restart
  std::string currentPath;     // deck under review

  void reload();
  int findDeck(const std::string& path) const;
  void startReview(int itemIndex, bool resume);
  FlashcardNextDeck resolveNextDeck();
  static FlashcardNextDeck resolveNextDeckThunk(void* self);

  void loopInput();

 public:
  explicit FlashcardLinearBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardLinearBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
