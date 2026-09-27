#pragma once
#include <string>
#include <vector>

#include "FlashcardCatalog.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Settings > Spaced review > Decks: every deck grouped by folder with its
// Spaced state (Active / Reviews only / Stand-by) and its share of the daily
// new cards. A folder row sets the state of all its decks at once; a deck row
// opens its own two settings. Saved on exit.
class FlashcardDeckOptionsActivity final : public Activity {
  struct Item {
    bool isFolder = false;
    std::string folder;
    int entry = -1;  // deck: index into entries
  };

  ButtonNavigator buttonNavigator;
  std::vector<FlashcardCatalog::Entry> entries;
  std::vector<Item> items;
  int selectedIndex = 0;
  uint32_t today = 0;
  bool dirty = false;

  // Deck page
  int deckEntry = -1;  // >= 0 while a deck's page is open
  int deckRow = 0;     // 0 = state, 1 = new cards per day
  bool editingShare = false;

  void reload();
  void replan();
  FlashcardDeckState folderState(const std::string& folder, bool& mixed) const;
  void setFolderState(const std::string& folder, FlashcardDeckState state);
  void loopDeck();
  void renderDeck(int top, int height);

 public:
  explicit FlashcardDeckOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardDeckOptions", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
