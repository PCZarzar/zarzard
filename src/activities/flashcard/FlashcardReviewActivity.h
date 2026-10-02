#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardCardActions.h"
#include "FlashcardDeck.h"
#include "FlashcardFolderIndex.h"
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
  // A deckPath ending in '/' is a whole folder (Linear only): every card of its
  // decks in one pass, always shuffled, with its own saved progress.
  // FLASHCARD_HARD_KEY is the difficult cards of every deck (Linear only).
  FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& deckPath,
                          ReviewMode mode, bool shuffle, bool resumeLinear, FlashcardNextDeckProvider nextDeck);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class CardState { Front, Back, Summary, Menu, ConfirmDelete, ConfirmDeleteMarked };
  enum class SummaryOption { Repeat, Full, DeleteMarked, NextDeck, Exit };
  enum class MenuItem { ToggleMark, Suspend, Skip, Reset, Delete };

  void flipCard();
  void rateCard(SrsRating rating);
  // True when everything that needed saving reached the SD card.
  bool saveProgress();
  void startSpacedSession();
  void showDone();
  void markLinear(LinearPile pile);
  void loopLinear();
  void loopSummary();
  void buildSummaryOptions();
  void finishWithNextDeck();
  void saveLinearProgress();
  size_t linearDeckSize() const;
  uint32_t linearFingerprint() const;
  // Makes the card on screen available (a whole folder reads it from the SD
  // card); called from loop(), never from render().
  void prepareLinearCard();
  const std::string& displayName() const;
  uint16_t cardSeconds();
  void startCardTimer();
  void resumeCardTimer();  // after the card menu: shift the timer by the time spent in it
  const char* doneMessage() const;
  bool saveFailed = false;  // the last save did not reach the SD card: say so

  // Card actions (hold side Up: mark, hold side Down: menu).
  bool onCardScreen() const { return cardState == CardState::Front || cardState == CardState::Back; }
  bool handleHold();
  void openMenu();
  void loopMenu();
  void loopConfirm();
  FlashcardCard* currentCard();
  bool canChangeCards() const;
  void doCardAction(FlashcardCardAction action);
  void toggleMark();
  void skipCard();
  // Takes the card on screen out of this session and shows the next one.
  void removeCurrentFromSession(bool deckChanged);
  void afterCardRemoved();
  size_t markedInSession() const;
  void deleteMarked();
  void renderMenu();
  void renderConfirm();
  void renderCard(const FlashcardCard& card);
  void renderSummary();

  FlashcardDeck deck;
  std::string deckPath;
  ReviewMode mode;
  bool shuffle;
  bool resumeLinear;
  FlashcardNextDeckProvider nextDeckProvider;
  bool deckLoaded = false;
  bool wholeFolder = false;              // deckPath ends in '/': Linear over a folder index
  FlashcardFolderIndex folderIndex;      // whole folder: where each card is on the SD card
  std::vector<std::string> folderDecks;  // decks of a whole folder or a Selection, in order
  std::string folderName;
  FlashcardCard folderCard;  // whole folder: the card on screen
  size_t folderCardIndex = SIZE_MAX;
  bool folderCardOk = false;
  unsigned long cardShownMs = 0;   // when the current card appeared (study time)
  unsigned long flippedMs = 0;     // when its back was shown; 0 while on the front
  unsigned long menuOpenedMs = 0;  // the card menu's time is not study time

  // Linear over one deck: the cards reviewed (not suspended), by position.
  std::vector<size_t> linearMap;
  // Linear: cards were suspended or deleted; the saved pass is renumbered
  // without them (FlashcardLinearSession::dropCards) so it survives.
  bool structureChanged = false;
  std::vector<size_t> removedLinear;  // linear indices deleted or suspended this session
  // Whole folder / difficult cards: changes to apply to the deck files on exit
  // (their record positions must stay valid while the session reads them).
  std::vector<FlashcardCardActions::Pending> pendingActions;
  std::vector<MenuItem> menuItems;
  int menuSelected = 0;
  CardState menuReturn = CardState::Front;
  const char* statusMsg = nullptr;  // one-line note over the next card

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
