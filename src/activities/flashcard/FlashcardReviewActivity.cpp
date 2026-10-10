#include "FlashcardReviewActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "FlashcardCatalog.h"
#include "FlashcardClock.h"
#include "FlashcardDeckOptions.h"
#include "FlashcardDoneActivity.h"
#include "FlashcardHistory.h"
#include "FlashcardLayout.h"
#include "FlashcardLinearProgress.h"
#include "FlashcardStatsScan.h"
#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/themes/zarzard/ZarzardFlashcards.h"
#include "components/themes/zarzard/ZarzardUi.h"
#include "fontIds.h"

namespace {
constexpr uint8_t SAVE_EVERY_RATINGS = 5;
constexpr unsigned long HOLD_MS = 700;  // side button held this long: card action

// Text area inside the card frame.
constexpr int TEXT_LEFT = ZarzardUi::CARD_FRAME_GUTTER + ZarzardUi::CARD_TEXT_INSET;
FlashcardLayout::CardArea cardArea(const GfxRenderer& renderer, const int frameTop, const int frameBottom) {
  return {frameTop + 12, frameBottom - 10, renderer.getScreenWidth() - 2 * TEXT_LEFT, 12};
}

void drawCenteredLines(const GfxRenderer& renderer, int font, int y, const std::vector<std::string>& lines) {
  const int lineH = renderer.getLineHeight(font);
  for (const auto& line : lines) {
    renderer.drawCenteredText(font, y, line.c_str());
    y += lineH;
  }
}
}  // namespace

FlashcardReviewActivity::FlashcardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const std::string& deckPath, ReviewMode mode, bool shuffle,
                                                 bool resumeLinear, FlashcardNextDeckProvider nextDeck)
    : Activity("FlashcardReview", renderer, mappedInput),
      deckPath(deckPath),
      mode(mode),
      shuffle(shuffle),
      resumeLinear(resumeLinear),
      nextDeckProvider(nextDeck) {}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void FlashcardReviewActivity::onEnter() {
  Activity::onEnter();
  std::string selectionDir;
  const bool selection = FlashcardLinearProgress::isSelectionKey(deckPath, selectionDir);
  wholeFolder = deckPath == FLASHCARD_HARD_KEY || selection || (!deckPath.empty() && deckPath.back() == '/');
  startCardTimer();
  if (deckPath == FLASHCARD_HARD_KEY) {
    // The difficult cards of every deck, read from their CSVs like a whole folder.
    folderName = tr(STR_FLASHCARD_HARD_CARDS);
    std::vector<FlashcardFolderIndex::CardRef> refs;
    if (mode == ReviewMode::Linear) {
      for (const auto& c : FlashcardStatsScan::collectHard(FlashcardCatalog::allDecks())) {
        refs.push_back(FlashcardFolderIndex::CardRef{c.path, c.offset});
      }
    }
    deckLoaded = mode == ReviewMode::Linear && folderIndex.buildFromRefs(refs);
  } else if (selection) {
    // Chosen decks of a folder, in list order or shuffled, as the user picked.
    folderName = tr(STR_ZZ_SELECTION);
    folderDecks = FlashcardLinearProgress::loadSelection(selectionDir);
    deckLoaded = mode == ReviewMode::Linear && !folderDecks.empty() && folderIndex.build(folderDecks);
  } else if (wholeFolder) {
    const std::string dir = deckPath.substr(0, deckPath.size() - 1);
    folderName = dir.substr(dir.rfind('/') + 1);
    // Only card positions are kept in RAM, so any folder size fits.
    folderDecks = FlashcardDeck::listDecks(dir);
    deckLoaded = mode == ReviewMode::Linear && folderIndex.build(folderDecks);
  } else {
    deckLoaded = deck.loadFromCsv(deckPath.c_str());
    // Linear reviews the cards that are not suspended, by position.
    if (deckLoaded && mode == ReviewMode::Linear) linearMap = deck.activeIndices();
  }
  cardState = CardState::Front;

  if (mode == ReviewMode::Linear) {
    // Linear never reads the clock and never writes the deck; its progress
    // lives in a separate file (FlashcardLinearProgress).
    bool restored = false;
    if (deckLoaded && resumeLinear) {
      FlashcardLinearProgress::Saved saved;
      if (FlashcardLinearProgress::load(deckPath, saved)) {
        // A saved pass keeps its order; whole-folder passes saved before the
        // order was recorded were always shuffled.
        if (saved.shuffle >= 0) {
          shuffle = saved.shuffle != 0;
        } else if (wholeFolder && !selection && deckPath != FLASHCARD_HARD_KEY) {
          shuffle = true;
        }
        if (saved.fingerprint == linearFingerprint()) {
          restored = linear.restore(saved.state, linearDeckSize(), shuffle);
        }
      }
      if (!restored) LOG_INF("FCRV", "Saved linear pass unusable (deck changed?), starting over: %s", deckPath.c_str());
    }
    if (!deckLoaded || linearDeckSize() == 0) {
      showDone();  // explains why there is nothing to study
      return;
    }
    if (!restored) {
      FlashcardLinearProgress::clear(deckPath);
      linear.start(linearDeckSize(), shuffle, esp_random());
    }
    if (linear.passDone()) {
      buildSummaryOptions();
      cardState = CardState::Summary;
    } else {
      prepareLinearCard();
    }
    requestUpdate();
    return;
  }

  startSpacedSession();
}

// Today's Spaced queue: due cards first, then new cards within today's quota.
void FlashcardReviewActivity::startSpacedSession() {
  today = FlashcardSrs::getToday();
  if (!deckLoaded || deck.isTruncated() || today == 0) {
    // Nothing that could be saved, or no trustworthy date (mode selection
    // blocks both): nothing to rate.
    reviewQueue.clear();
    showDone();
    return;
  }
  // New cards: the deck's share of today's total (the list refreshed the
  // split), never beyond what the total still allows.
  const uint16_t totalLeft = FlashcardDeck::newCardsAvailable(FLASHCARD_STORE.newPerDay, FLASHCARD_STORE.newTodayCount,
                                                              FLASHCARD_STORE.newTodayDate, today);
  const uint16_t newAvailable = std::min(totalLeft, FLASHCARD_DECK_OPTIONS.newRemaining(deckPath, today));
  reviewQueue = deck.buildReviewQueue(today, static_cast<uint8_t>(std::min<uint16_t>(newAvailable, 255)),
                                      FLASHCARD_STORE.maxReviewPerDay);
  currentQueueIndex = 0;

  if (reviewQueue.empty()) {
    showDone();
    return;
  }
  maxQueueSize = reviewQueue.size() * 2;
  requestUpdate();
}

void FlashcardReviewActivity::onExit() {
  // Also runs when the device goes to sleep mid-session: keep every rating
  // and the linear position.
  if (mode == ReviewMode::Spaced && (unsavedRatings > 0 || newQuotaDirty)) saveProgress();
  // Deck files first: a whole folder's saved pass is renumbered against them.
  if (!pendingActions.empty()) {
    const size_t applied = FlashcardCardActions::applyToFiles(pendingActions);
    if (applied < pendingActions.size()) {
      LOG_ERR("FCRV", "%u of %u card actions not saved", (unsigned)(pendingActions.size() - applied),
              (unsigned)pendingActions.size());
    }
    pendingActions.clear();
  }
  if (mode == ReviewMode::Linear && (linearDirty || structureChanged)) saveLinearProgress();
  FlashcardHistory::flush();
  Activity::onExit();
}

void FlashcardReviewActivity::saveLinearProgress() {
  if (!deckLoaded) return;
  FlashcardLinearProgress::Saved saved;
  saved.state = linear.exportState();
  saved.shuffle = linear.shuffled() ? 1 : 0;
  saved.fingerprint = linearFingerprint();
  if (structureChanged) {
    // Cards left the deck: save the pass renumbered without them, so it (and
    // its Missed/Unsure piles) can still be resumed.
    bool ok = deckPath != FLASHCARD_HARD_KEY && FlashcardLinearSession::dropCards(saved.state, removedLinear);
    if (ok && wholeFolder) {
      if (!pendingActions.empty()) return;  // the decks change on exit; saved then
      FlashcardFolderIndex fresh;
      ok = fresh.build(folderDecks);
      std::vector<size_t> gone = removedLinear;
      std::sort(gone.begin(), gone.end());
      gone.erase(std::unique(gone.begin(), gone.end()), gone.end());
      ok = ok && fresh.size() + gone.size() == folderIndex.size();
      saved.fingerprint = fresh.fingerprint();
    } else if (ok) {
      saved.fingerprint = deck.fingerprintOf(deck.activeIndices());
    }
    if (!ok) {
      LOG_INF("FCRV", "Linear pass cannot follow the changed deck, clearing: %s", deckPath.c_str());
      FlashcardLinearProgress::clear(deckPath);
      FlashcardHistory::flush();
      linearDirty = false;
      marksSinceSave = 0;
      return;
    }
  }
  FlashcardHistory::flush();
  if (FlashcardLinearProgress::save(deckPath, saved)) {
    linearDirty = false;
    marksSinceSave = 0;
    saveFailed = false;
  } else {
    saveFailed = true;
    statusMsg = tr(STR_ZZ_SAVE_FAILED);  // kept dirty: retried on the next save
  }
}

// ─── Input ───────────────────────────────────────────────────────────────────

void FlashcardReviewActivity::loop() {
  if (mode == ReviewMode::Spaced && pendingSave) {
    if (!saveProgress()) requestUpdate();  // shows the failure over the card
    pendingSave = false;
  }
  if (cardState == CardState::Menu) {
    loopMenu();
    return;
  }
  if (cardState == CardState::ConfirmDelete || cardState == CardState::ConfirmDeleteMarked) {
    loopConfirm();
    return;
  }
  if (onCardScreen() && handleHold()) return;

  if (mode == ReviewMode::Linear) {
    loopLinear();
    return;
  }

  if (reviewQueue.empty() || currentQueueIndex >= reviewQueue.size()) return;

  if (cardState == CardState::Front) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      saveProgress();
      finish();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) flipCard();
    return;
  }

  // Back side: Back→Again, Left/Up→Hard, Right/Down→Good, Confirm→Easy
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    {
      RenderLock lock(*this);  // changes what render() reads
      rateCard(SrsRating::Again);
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
             mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    {
      RenderLock lock(*this);  // changes what render() reads
      rateCard(SrsRating::Hard);
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
             mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    {
      RenderLock lock(*this);  // changes what render() reads
      rateCard(SrsRating::Good);
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    {
      RenderLock lock(*this);  // changes what render() reads
      rateCard(SrsRating::Easy);
    }
  }
}

void FlashcardReviewActivity::loopLinear() {
  switch (cardState) {
    case CardState::Front:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        finish();  // onExit saves the position
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        flipCard();
      }
      break;
    case CardState::Back:
      // Back→Missed, Left/Up→Unsure, Right/Down→Known, Confirm→nothing
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        {
          RenderLock lock(*this);  // changes what render() reads
          markLinear(LinearPile::Missed);
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                 mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        {
          RenderLock lock(*this);  // changes what render() reads
          markLinear(LinearPile::Unsure);
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                 mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        {
          RenderLock lock(*this);  // changes what render() reads
          markLinear(LinearPile::Known);
        }
      }
      break;
    case CardState::Summary:
      loopSummary();
      break;
  }
}

void FlashcardReviewActivity::loopSummary() {
  const int count = static_cast<int>(summaryOptions.size());
  buttonNavigator.onNext([this, count] {
    summarySelected = ButtonNavigator::nextIndex(summarySelected, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    summarySelected = ButtonNavigator::previousIndex(summarySelected, count);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;

  RenderLock lock(*this);  // a new pass changes what render() reads
  switch (summaryOptions[summarySelected]) {
    case SummaryOption::Repeat:
      linear.startRepeatPass(esp_random());
      linearDirty = true;
      break;
    case SummaryOption::Full:
      linear.startFullPass(esp_random());
      linearDirty = true;
      break;
    case SummaryOption::DeleteMarked:
      cardState = CardState::ConfirmDeleteMarked;
      requestUpdate();
      return;
    case SummaryOption::NextDeck:
      finishWithNextDeck();
      return;
    case SummaryOption::Exit:
      finish();
      return;
  }
  startCardTimer();  // time on the summary screen is not study time
  if (linear.passDone()) {
    buildSummaryOptions();
    cardState = CardState::Summary;
  } else {
    cardState = CardState::Front;
    prepareLinearCard();
  }
  requestUpdate();
}

// ─── State transitions ───────────────────────────────────────────────────────

void FlashcardReviewActivity::flipCard() {
  statusMsg = nullptr;
  flippedMs = millis();
  cardState = CardState::Back;
  requestUpdate();
}

void FlashcardReviewActivity::rateCard(SrsRating rating) {
  statusMsg = nullptr;
  // Studying past midnight: what was rated so far stays on its day, this card
  // is rated on the new one, then the queue is rebuilt for the new day.
  const uint32_t now = FlashcardSrs::getToday();
  const bool newDay = now != 0 && now != today;
  if (newDay) {
    saveProgress();
    today = now;
  }
  const size_t cardIdx = reviewQueue[currentQueueIndex];
  auto& card = deck.getCards()[cardIdx];
  const bool wasNew = card.isNew();
  const uint16_t intervalBefore = card.srs.interval;
  ratingCounts[static_cast<size_t>(rating)]++;
  FlashcardHistory::recordSpaced(today, rating, wasNew, intervalBefore, cardSeconds());
  SrsState next = FlashcardSrs::review(card.srs, rating, today);
  // SM-2 is unchanged; only a due date on a free day moves to a study day.
  next.dueDate = FlashcardClock::avoidFreeDays(next.dueDate, today, next.interval, FLASHCARD_STORE.freeDaysMask);
  deck.updateCard(cardIdx, next);

  // First rating of a new card consumes the daily new-card quota.
  if (wasNew) {
    FLASHCARD_STORE.newTodayCount++;
    FLASHCARD_DECK_OPTIONS.recordNew(deckPath, today);
    newQuotaDirty = true;
  }

  if (rating == SrsRating::Again && reviewQueue.size() < maxQueueSize) {
    reviewQueue.push_back(cardIdx);  // see the failed card again at the end of the session
  }

  currentQueueIndex++;
  if (++unsavedRatings >= SAVE_EVERY_RATINGS) pendingSave = true;  // flushed by loop() after the render

  if (newDay) {
    saveProgress();
    pendingSave = false;
    cardState = CardState::Front;
    startSpacedSession();
    return;
  }

  if (currentQueueIndex >= reviewQueue.size()) {
    saveProgress();
    pendingSave = false;
    showDone();
  } else {
    cardState = CardState::Front;
    requestUpdate();
  }
}

// Spaced only: persist the deck and, with it, the daily new-card counter. What
// fails stays dirty in RAM, is retried on the next save, and is reported.
bool FlashcardReviewActivity::saveProgress() {
  if (!deckLoaded) return true;  // never overwrite a CSV we could not read
  bool ok = true;
  if (unsavedRatings > 0) {
    if (deck.saveToCsv(deckPath.c_str())) {
      unsavedRatings = 0;
      FLASHCARD_STORE.homeCardsDay = 0;  // today's count changed: Home and the hub count again
    } else {
      LOG_ERR("FCRV", "Failed to save %s", deckPath.c_str());
      ok = false;
    }
  }
  if (newQuotaDirty) {
    const bool quotas = FLASHCARD_STORE.saveToFile() && FLASHCARD_DECK_OPTIONS.saveToFile();
    if (quotas) newQuotaDirty = false;
    ok = ok && quotas;
  }
  ok = FlashcardHistory::flush() && ok;
  saveFailed = !ok;
  if (!ok) statusMsg = tr(STR_ZZ_SAVE_FAILED);
  return ok;
}

// startActivityForResult so Done pops Review too when it finishes; Done's
// result (next deck or not) is forwarded to the deck list.
void FlashcardReviewActivity::showDone() {
  const FlashcardNextDeck next = nextDeckProvider();
  startActivityForResult(
      std::make_unique<FlashcardDoneActivity>(renderer, mappedInput, ratingCounts, next, doneMessage()),
      [this](const ActivityResult& result) {
        const auto* menu = std::get_if<MenuResult>(&result.data);
        if (menu && menu->action == RESULT_NEXT_DECK) {
          finishWithNextDeck();
        } else {
          finish();
        }
      });
}

void FlashcardReviewActivity::markLinear(LinearPile pile) {
  statusMsg = nullptr;
  linear.mark(pile);
  linearDirty = true;
  FlashcardHistory::recordLinear(FlashcardSrs::getToday(), cardSeconds());
  // Periodic save so a flat battery loses at most a few cards of progress.
  if (++marksSinceSave >= SAVE_EVERY_RATINGS) saveLinearProgress();
  if (linear.passDone() && !wholeFolder && linear.passSize() == linearMap.size() - linear.excludedCount()) {
    // A full pass over the deck: its result is the deck's Linear mastery.
    FLASHCARD_DECK_OPTIONS.recordLinearPass(deckPath, linear.knownCount(), linear.passSize());
    FLASHCARD_DECK_OPTIONS.saveToFile();
  }
  if (linear.passDone()) {
    buildSummaryOptions();
    cardState = CardState::Summary;
  } else {
    cardState = CardState::Front;
    prepareLinearCard();
  }
  requestUpdate();
}

const char* FlashcardReviewActivity::doneMessage() const {
  if (saveFailed) return tr(STR_ZZ_SAVE_FAILED);
  if (deckLoaded && mode == ReviewMode::Linear && linearDeckSize() == 0) return tr(STR_FLASHCARD_ALL_SUSPENDED);
  if (deckLoaded) return nullptr;
  return deckPath == FLASHCARD_HARD_KEY ? tr(STR_FLASHCARD_NO_HARD) : tr(STR_FLASHCARD_NOT_LOADED);
}

// Seconds on the card just answered: time on the front plus time reading the
// back, each capped (FlashcardHistory::MAX_SIDE_SECONDS); then times the next.
uint16_t FlashcardReviewActivity::cardSeconds() {
  const unsigned long now = millis();
  const unsigned long frontEnd = flippedMs != 0 ? flippedMs : now;
  const unsigned long front = (frontEnd - cardShownMs) / 1000;
  const unsigned long back = flippedMs != 0 ? (now - flippedMs) / 1000 : 0;
  constexpr unsigned long CAP = FlashcardHistory::MAX_SIDE_SECONDS;
  startCardTimer();
  return static_cast<uint16_t>(std::min(front, CAP) + std::min(back, CAP));
}

void FlashcardReviewActivity::startCardTimer() {
  cardShownMs = millis();
  flippedMs = 0;
}

void FlashcardReviewActivity::resumeCardTimer() {
  const unsigned long spent = millis() - menuOpenedMs;
  cardShownMs += spent;
  if (flippedMs != 0) flippedMs += spent;
}

size_t FlashcardReviewActivity::linearDeckSize() const { return wholeFolder ? folderIndex.size() : linearMap.size(); }

uint32_t FlashcardReviewActivity::linearFingerprint() const {
  // Over the reviewed cards: equal to the whole-deck hash when none is suspended.
  return wholeFolder ? folderIndex.fingerprint() : deck.fingerprintOf(linearMap);
}

const std::string& FlashcardReviewActivity::displayName() const { return wholeFolder ? folderName : deck.getName(); }

void FlashcardReviewActivity::prepareLinearCard() {
  if (!wholeFolder || linear.passDone()) return;
  const size_t idx = linear.current();
  if (idx == folderCardIndex) return;
  folderCardIndex = idx;
  folderCardOk = folderIndex.loadCard(idx, folderCard);
  if (!folderCardOk) {
    folderCard = FlashcardCard{};
    folderCard.frontContent = tr(STR_FLASHCARD_NOT_LOADED);
  }
}

void FlashcardReviewActivity::buildSummaryOptions() {
  nextDeckName = nextDeckProvider().name;
  summaryOptions.clear();
  summaryOptions.reserve(5);
  if (linear.canRepeat()) summaryOptions.push_back(SummaryOption::Repeat);
  summaryOptions.push_back(SummaryOption::Full);
  if (markedInSession() > 0 && canChangeCards()) summaryOptions.push_back(SummaryOption::DeleteMarked);
  if (!nextDeckName.empty()) summaryOptions.push_back(SummaryOption::NextDeck);
  summaryOptions.push_back(SummaryOption::Exit);
  summarySelected = 0;
}

void FlashcardReviewActivity::finishWithNextDeck() {
  MenuResult r;
  r.action = RESULT_NEXT_DECK;
  setResult(std::move(r));
  finish();
}

// ─── Card actions ────────────────────────────────────────────────────────────

FlashcardCard* FlashcardReviewActivity::currentCard() {
  if (mode == ReviewMode::Spaced) {
    if (reviewQueue.empty() || currentQueueIndex >= reviewQueue.size()) return nullptr;
    return &deck.getCards()[reviewQueue[currentQueueIndex]];
  }
  if (!deckLoaded || linear.passDone()) return nullptr;
  if (wholeFolder) return folderCardOk ? &folderCard : nullptr;
  return &deck.getCards()[linearMap[linear.current()]];
}

// A deck over the card limit cannot be saved; a whole folder saves each
// card's own deck on exit (decks that cannot be saved are skipped there).
bool FlashcardReviewActivity::canChangeCards() const { return deckLoaded && (wholeFolder || !deck.isTruncated()); }

bool FlashcardReviewActivity::handleHold() {
  if (!currentCard()) return false;
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Up, HOLD_MS)) {
    RenderLock lock(*this);  // changes what render() reads
    toggleMark();
    return true;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Down, HOLD_MS)) {
    RenderLock lock(*this);
    openMenu();
    return true;
  }
  return false;
}

void FlashcardReviewActivity::toggleMark() {
  FlashcardCard* card = currentCard();
  if (!card) return;
  if (!canChangeCards()) {
    statusMsg = tr(STR_FLASHCARD_ACTION_UNAVAILABLE);
  } else {
    const bool mark = !card->isMarked();
    doCardAction(mark ? FlashcardCardAction::Mark : FlashcardCardAction::Unmark);
    statusMsg = mark ? tr(STR_FLASHCARD_CARD_MARKED) : tr(STR_FLASHCARD_CARD_UNMARKED);
  }
  requestUpdate();
}

void FlashcardReviewActivity::openMenu() {
  menuItems.clear();
  if (canChangeCards()) {
    menuItems = {MenuItem::ToggleMark, MenuItem::Suspend, MenuItem::Skip, MenuItem::Reset, MenuItem::Delete};
  } else {
    menuItems = {MenuItem::Skip};
  }
  menuSelected = 0;
  menuReturn = cardState;
  menuOpenedMs = millis();
  cardState = CardState::Menu;
  requestUpdate();
}

void FlashcardReviewActivity::doCardAction(const FlashcardCardAction action) {
  FlashcardCard* card = currentCard();
  if (!card || !canChangeCards()) return;
  const bool leaves = action == FlashcardCardAction::Suspend || action == FlashcardCardAction::Delete;
  if (mode == ReviewMode::Spaced) {
    FlashcardCardActions::apply(*card, action);
    ++unsavedRatings;  // the deck is written with the next save
    pendingSave = true;
    // A reset card is new again: it comes back through the daily new cards.
    if (leaves || action == FlashcardCardAction::Reset) removeCurrentFromSession(false);
    return;
  }
  if (!wholeFolder) {
    FlashcardCardActions::apply(*card, action);
    if (!deck.saveToCsv(deckPath.c_str())) LOG_ERR("FCRV", "Failed to save %s", deckPath.c_str());
  } else {
    std::string path;
    uint32_t offset = 0;
    if (folderIndex.refOf(linear.current(), path, offset)) pendingActions.push_back({path, offset, action});
    FlashcardCardActions::apply(folderCard, action);  // RAM copy of the card on screen
    folderIndex.setFlags(linear.current(), folderCard.flags);
  }
  if (leaves) removeCurrentFromSession(true);
}

void FlashcardReviewActivity::skipCard() {
  if (!currentCard()) return;
  removeCurrentFromSession(false);
}

void FlashcardReviewActivity::removeCurrentFromSession(const bool deckChanged) {
  if (mode == ReviewMode::Spaced) {
    const size_t idx = reviewQueue[currentQueueIndex];
    // Here and wherever a miss re-queued it later in the session.
    for (size_t i = currentQueueIndex; i < reviewQueue.size();) {
      if (reviewQueue[i] == idx) {
        reviewQueue.erase(reviewQueue.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }
  } else {
    if (deckChanged) removedLinear.push_back(linear.current());
    linear.exclude(linear.current());
    if (deckChanged) {
      structureChanged = true;
    } else {
      linearDirty = true;
    }
  }
  afterCardRemoved();
}

void FlashcardReviewActivity::afterCardRemoved() {
  cardState = CardState::Front;
  startCardTimer();
  if (mode == ReviewMode::Spaced) {
    if (currentQueueIndex >= reviewQueue.size()) {
      saveProgress();
      pendingSave = false;
      showDone();
      return;
    }
  } else if (linear.passDone()) {
    buildSummaryOptions();
    cardState = CardState::Summary;
  } else {
    prepareLinearCard();
  }
  requestUpdate();
}

size_t FlashcardReviewActivity::markedInSession() const {
  if (mode != ReviewMode::Linear || !deckLoaded) return 0;
  size_t n = 0;
  const size_t count = linearDeckSize();
  for (size_t j = 0; j < count; ++j) {
    if (linear.isExcluded(j)) continue;
    const bool marked =
        wholeFolder ? (folderIndex.flagsOf(j) & FlashcardCard::FLAG_MARKED) : deck.getCards()[linearMap[j]].isMarked();
    if (marked) ++n;
  }
  return n;
}

void FlashcardReviewActivity::deleteMarked() {
  size_t n = 0;
  const size_t count = linearDeckSize();
  for (size_t j = 0; j < count; ++j) {
    if (linear.isExcluded(j)) continue;
    if (wholeFolder) {
      if (!(folderIndex.flagsOf(j) & FlashcardCard::FLAG_MARKED)) continue;
      std::string path;
      uint32_t offset = 0;
      if (folderIndex.refOf(j, path, offset)) pendingActions.push_back({path, offset, FlashcardCardAction::Delete});
      folderIndex.setFlags(j, folderIndex.flagsOf(j) | FlashcardCard::FLAG_DELETED);
    } else {
      auto& card = deck.getCards()[linearMap[j]];
      if (!card.isMarked()) continue;
      FlashcardCardActions::apply(card, FlashcardCardAction::Delete);
    }
    removedLinear.push_back(j);
    linear.exclude(j);
    ++n;
  }
  if (n > 0) {
    structureChanged = true;
    if (!wholeFolder && !deck.saveToCsv(deckPath.c_str())) LOG_ERR("FCRV", "Failed to save %s", deckPath.c_str());
    statusMsg = tr(STR_FLASHCARD_MARKED_DELETED);
  }
  buildSummaryOptions();
  cardState = CardState::Summary;
  requestUpdate();
}

void FlashcardReviewActivity::loopMenu() {
  const int count = static_cast<int>(menuItems.size());
  buttonNavigator.onNext([this, count] {
    menuSelected = ButtonNavigator::nextIndex(menuSelected, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    menuSelected = ButtonNavigator::previousIndex(menuSelected, count);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    resumeCardTimer();
    cardState = menuReturn;
    requestUpdate();
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm) || menuItems.empty()) return;
  RenderLock lock(*this);  // card actions change what render() reads
  switch (menuItems[menuSelected]) {
    case MenuItem::ToggleMark:
      resumeCardTimer();
      cardState = menuReturn;
      toggleMark();
      break;
    case MenuItem::Suspend:
      statusMsg = tr(STR_FLASHCARD_CARD_SUSPENDED);
      doCardAction(FlashcardCardAction::Suspend);
      break;
    case MenuItem::Skip:
      statusMsg = tr(STR_FLASHCARD_CARD_SKIPPED);
      skipCard();
      break;
    case MenuItem::Reset:
      statusMsg = tr(STR_FLASHCARD_CARD_RESET);
      resumeCardTimer();  // Linear stays on the card; Spaced moves on and restarts it
      cardState = menuReturn;
      doCardAction(FlashcardCardAction::Reset);
      requestUpdate();
      break;
    case MenuItem::Delete:
      cardState = CardState::ConfirmDelete;
      requestUpdate();
      break;
  }
}

void FlashcardReviewActivity::loopConfirm() {
  const bool single = cardState == CardState::ConfirmDelete;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cardState = single ? CardState::Menu : CardState::Summary;
    requestUpdate();
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;
  RenderLock lock(*this);  // deleting changes what render() reads
  if (single) {
    statusMsg = tr(STR_FLASHCARD_CARD_DELETED);
    cardState = menuReturn;
    doCardAction(FlashcardCardAction::Delete);
  } else {
    deleteMarked();
  }
}

void FlashcardReviewActivity::renderMenu() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, displayName().c_str(),
                 tr(STR_FLASHCARD_CARD_MENU));
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const FlashcardCard* card = currentCard();
  if (card) {
    const std::string front =
        renderer.truncatedText(UI_12_FONT_ID, card->frontMain().c_str(), pageWidth - metrics.contentSidePadding * 2);
    renderer.drawCenteredText(UI_12_FONT_ID, y, front.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
  }
  if (!canChangeCards()) {
    renderer.drawCenteredText(SMALL_FONT_ID, y, tr(STR_FLASHCARD_ACTION_UNAVAILABLE));
    y += renderer.getLineHeight(SMALL_FONT_ID) + 6;
  }
  FlashcardUi::Row rows[5];
  const int n = static_cast<int>(std::min<size_t>(menuItems.size(), 5));
  for (int i = 0; i < n; ++i) {
    switch (menuItems[i]) {
      case MenuItem::ToggleMark:
        rows[i].title = card && card->isMarked() ? tr(STR_FLASHCARD_UNMARK) : tr(STR_FLASHCARD_MARK);
        rows[i].subtitle = tr(STR_FLASHCARD_MARK_DESC);
        break;
      case MenuItem::Suspend:
        rows[i].title = tr(STR_FLASHCARD_SUSPEND);
        rows[i].subtitle = tr(STR_FLASHCARD_SUSPEND_DESC);
        break;
      case MenuItem::Skip:
        rows[i].title = tr(STR_FLASHCARD_SKIP);
        rows[i].subtitle = tr(STR_FLASHCARD_SKIP_DESC);
        break;
      case MenuItem::Reset:
        rows[i].title = tr(STR_FLASHCARD_RESET_CARD);
        rows[i].subtitle = tr(STR_FLASHCARD_RESET_DESC);
        break;
      case MenuItem::Delete:
        rows[i].title = tr(STR_DELETE);
        rows[i].subtitle = tr(STR_FLASHCARD_DELETE_DESC);
        break;
    }
  }
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  FlashcardUi::drawRows(renderer, Rect{0, y, pageWidth, bottom - y}, rows, n, menuSelected);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void FlashcardReviewActivity::renderConfirm() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int midY = renderer.getScreenHeight() / 3;
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, displayName().c_str());
  char title[64];
  if (cardState == CardState::ConfirmDelete) {
    snprintf(title, sizeof(title), "%s", tr(STR_FLASHCARD_DELETE_CONFIRM));
  } else {
    snprintf(title, sizeof(title), "%s (%u)", tr(STR_FLASHCARD_DELETE_MARKED_CONFIRM),
             static_cast<unsigned>(markedInSession()));
  }
  renderer.drawCenteredText(UI_12_FONT_ID, midY, title, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, midY + renderer.getLineHeight(UI_12_FONT_ID) + 8,
                            tr(STR_FLASHCARD_CANNOT_UNDO));
  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_DELETE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

// ─── Render ──────────────────────────────────────────────────────────────────

void FlashcardReviewActivity::render(RenderLock&&) {
  if (cardState == CardState::Menu) {
    renderMenu();
    return;
  }
  if (cardState == CardState::ConfirmDelete || cardState == CardState::ConfirmDeleteMarked) {
    renderConfirm();
    return;
  }
  if (mode == ReviewMode::Linear) {
    if (cardState == CardState::Summary) {
      renderSummary();
      return;
    }
    if (linear.passDone()) return;
    renderCard(wholeFolder ? folderCard : deck.getCards()[linearMap[linear.current()]]);
    return;
  }
  if (reviewQueue.empty() || currentQueueIndex >= reviewQueue.size()) return;
  renderCard(deck.getCards()[reviewQueue[currentQueueIndex]]);
}

void FlashcardReviewActivity::renderCard(const FlashcardCard& card) {
  renderer.clearScreen();

  char progress[32] = "";
  int percent = 0;
  if (mode == ReviewMode::Linear) {
    snprintf(progress, sizeof(progress), "%s %u \xC2\xB7 %u/%u", tr(STR_FLASHCARD_PASS),
             static_cast<unsigned>(linear.pass()), static_cast<unsigned>(linear.shownPosition()),
             static_cast<unsigned>(linear.passSize()));
    if (linear.passSize() > 0) percent = static_cast<int>(linear.shownPosition() * 100 / linear.passSize());
  } else {
    // Card i of the session (the queue grows when a card is missed).
    snprintf(progress, sizeof(progress), "%u/%u", static_cast<unsigned>(currentQueueIndex + 1),
             static_cast<unsigned>(reviewQueue.size()));
    if (!reviewQueue.empty()) percent = static_cast<int>((currentQueueIndex + 1) * 100 / reviewQueue.size());
  }
  // A card action's note takes the progress's place for one card.
  const char* right = statusMsg ? statusMsg : progress;
  const int frameTop = ZarzardUi::drawCardHeader(renderer, displayName().c_str(), right, percent);
  const int frameBottom = ZarzardUi::actionButtonsTop(renderer) - 6;
  ZarzardUi::drawCardFrame(renderer, frameTop, frameBottom);

  const auto area = cardArea(renderer, frameTop, frameBottom);
  if (card.isMarked()) {
    // Bookmark in the card's corner: set aside for the clean-up list.
    ZarzardUi::drawIcon(renderer, ZarzardIcons::BookmarkSimpleFill24,
                        renderer.getScreenWidth() - ZarzardUi::CARD_FRAME_GUTTER - 34, frameTop + 2);
  }
  std::string mainText, hint;
  card.parseFront(mainText, hint);

  // The buttons follow the front-button remap: labels and icons are mapped
  // the same way, then matched by position.
  char intervals[4][16] = {};  // Must outlive drawActionButtons below.
  ZarzardUi::ActionButton buttons[4];
  auto place = [this, &buttons](const char* const labels[4], const char* const subs[4],
                                const ZarzardIcons::Icon* const icons[4]) {
    static const char* const KEYS[4] = {"0", "1", "2", "3"};
    const auto mappedKeys = mappedInput.mapLabels(KEYS[0], KEYS[1], KEYS[2], KEYS[3]);
    const char* order[4] = {mappedKeys.btn1, mappedKeys.btn2, mappedKeys.btn3, mappedKeys.btn4};
    for (int pos = 0; pos < 4; ++pos) {
      const int role = order[pos] ? order[pos][0] - '0' : -1;
      if (role < 0 || role > 3 || !icons[role]) continue;
      buttons[pos].icon = icons[role];
      buttons[pos].label = labels[role];
      buttons[pos].sub = subs[role];
    }
  };

  if (cardState == CardState::Front) {
    const auto front = FlashcardLayout::layoutFront(renderer, mainText, hint, area, UI_10_FONT_ID);
    drawCenteredLines(renderer, front.mainFont, front.mainY, front.mainLines);
    drawCenteredLines(renderer, front.hintFont, front.hintY, front.hintLines);
    const char* const labels[4] = {tr(STR_BACK), tr(STR_FLASHCARD_SHOW), "", ""};
    const char* const subs[4] = {"", "", "", ""};
    const ZarzardIcons::Icon* const icons[4] = {&ZarzardIcons::ArrowUUpLeft20, &ZarzardIcons::Eye20, nullptr, nullptr};
    place(labels, subs, icons);
  } else {
    const auto back = FlashcardLayout::layoutBack(renderer, mainText, card.backContent, area, UI_12_FONT_ID);
    drawCenteredLines(renderer, back.questionFont, back.questionY, back.questionLines);
    // Dashed rule between question and answer.
    const int left = TEXT_LEFT, rightX = renderer.getScreenWidth() - TEXT_LEFT;
    for (int x = left; x < rightX; x += 10) renderer.fillRect(x, back.separatorY, 5, 2, true);
    drawCenteredLines(renderer, back.answerFont, back.answerY, back.answerLines);

    if (mode == ReviewMode::Linear) {
      const char* const labels[4] = {tr(STR_FLASHCARD_MISSED), "", tr(STR_FLASHCARD_UNSURE), tr(STR_FLASHCARD_KNOWN)};
      const char* const subs[4] = {"", "", "", ""};
      const ZarzardIcons::Icon* const icons[4] = {&ZarzardIcons::XCircle20, nullptr, &ZarzardIcons::Question20,
                                                  &ZarzardIcons::CheckCircle20};
      place(labels, subs, icons);
    } else {
      // Rating and the interval it leads to. Back→Again, Confirm→Easy, Previous→Hard, Next→Good
      const char* const labels[4] = {tr(STR_FLASHCARD_AGAIN), tr(STR_FLASHCARD_EASY), tr(STR_FLASHCARD_HARD),
                                     tr(STR_FLASHCARD_GOOD)};
      // previewInterval returns one static buffer: copy each result.
      const SrsRating ratings[4] = {SrsRating::Again, SrsRating::Easy, SrsRating::Hard, SrsRating::Good};
      for (int i = 0; i < 4; ++i) {
        const char* preview = FlashcardSrs::previewInterval(card.srs, ratings[i]);
        // "<10m" is no timer: the card just comes back later in this session.
        if (strcmp(preview, "<10m") == 0) preview = tr(STR_ZZ_TODAY);
        snprintf(intervals[i], sizeof(intervals[i]), "%s", preview);
      }
      const char* const subs[4] = {intervals[0], intervals[1], intervals[2], intervals[3]};
      const ZarzardIcons::Icon* const icons[4] = {&ZarzardIcons::XCircle20, &ZarzardIcons::Lightning20,
                                                  &ZarzardIcons::Question20, &ZarzardIcons::CheckCircle20};
      place(labels, subs, icons);
    }
  }
  ZarzardUi::drawActionButtons(renderer, buttons);
  renderer.displayBuffer();
}

void FlashcardReviewActivity::renderSummary() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  char title[32];
  snprintf(title, sizeof(title), "%s %u", tr(STR_FLASHCARD_PASS), static_cast<unsigned>(linear.pass()));
  int y = ZarzardUi::drawCardHeader(renderer, displayName().c_str(), title, 100) + 4;

  // Missed / Unsure / Known: one tile each.
  const size_t counts[3] = {linear.missedCount(), linear.unsureCount(), linear.knownCount()};
  const char* names[3] = {tr(STR_FLASHCARD_MISSED), tr(STR_FLASHCARD_UNSURE), tr(STR_FLASHCARD_KNOWN)};
  const ZarzardIcons::Icon* icons[3] = {&ZarzardIcons::XCircle32, &ZarzardIcons::Question32,
                                        &ZarzardIcons::CheckCircle32};
  constexpr int GAP = 12, TILE_H = 136;
  const int tileW = (pageWidth - 2 * ZarzardUi::GUTTER - 2 * GAP) / 3;
  for (int i = 0; i < 3; ++i) {
    const int x = ZarzardUi::GUTTER + i * (tileW + GAP);
    ZarzardUi::drawCard(renderer, x, y, tileW, TILE_H, false, 18);
    ZarzardUi::drawIcon(renderer, *icons[i], x + (tileW - 32) / 2, y + 14);
    char val[8];
    snprintf(val, sizeof(val), "%u", static_cast<unsigned>(counts[i]));
    renderer.drawText(ZZ_TITLE_18_FONT_ID, x + (tileW - renderer.getTextWidth(ZZ_TITLE_18_FONT_ID, val)) / 2, y + 52,
                      val);
    renderer.drawText(SMALL_FONT_ID, x + (tileW - renderer.getTextWidth(SMALL_FONT_ID, names[i])) / 2, y + 104,
                      names[i]);
  }
  y += TILE_H + metrics.verticalSpacing;
  if (statusMsg) renderer.drawCenteredText(SMALL_FONT_ID, y, statusMsg);
  y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing / 2;

  char nextLabel[96] = "";
  char deleteLabel[64] = "";
  snprintf(deleteLabel, sizeof(deleteLabel), "%s (%u)", tr(STR_FLASHCARD_DELETE_MARKED),
           static_cast<unsigned>(markedInSession()));
  snprintf(nextLabel, sizeof(nextLabel), "%s: %s", tr(STR_FLASHCARD_NEXT_DECK), nextDeckName.c_str());
  FlashcardUi::Row rows[5];
  const int count = static_cast<int>(std::min<size_t>(summaryOptions.size(), 5));
  for (int i = 0; i < count; ++i) {
    switch (summaryOptions[i]) {
      case SummaryOption::Repeat:
        rows[i].title = tr(STR_FLASHCARD_REPEAT_MISSED);
        break;
      case SummaryOption::Full:
        rows[i].title = tr(STR_FLASHCARD_FULL_PASS);
        break;
      case SummaryOption::DeleteMarked:
        rows[i].title = deleteLabel;
        break;
      case SummaryOption::NextDeck:
        rows[i].title = nextLabel;
        break;
      case SummaryOption::Exit:
        rows[i].title = tr(STR_FLASHCARD_EXIT);
        break;
    }
  }
  const int listH = pageHeight - y - metrics.buttonHintsHeight - metrics.verticalSpacing;
  FlashcardUi::drawRows(renderer, Rect{0, y, pageWidth, listH}, rows, count, summarySelected);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
