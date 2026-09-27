#include "FlashcardReviewActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>

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
#include "fontIds.h"

namespace {
constexpr uint8_t SAVE_EVERY_RATINGS = 5;

FlashcardLayout::CardArea cardArea(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return {metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing,
          renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing,
          renderer.getScreenWidth() - metrics.contentSidePadding * 2, metrics.verticalSpacing};
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
  wholeFolder = deckPath == FLASHCARD_HARD_KEY || (!deckPath.empty() && deckPath.back() == '/');
  cardShownMs = millis();
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
  } else if (wholeFolder) {
    const std::string dir = deckPath.substr(0, deckPath.size() - 1);
    folderName = dir.substr(dir.rfind('/') + 1);
    // Only card positions are kept in RAM, so any folder size fits.
    deckLoaded = mode == ReviewMode::Linear && folderIndex.build(FlashcardDeck::listDecks(dir));
    shuffle = true;  // a whole folder is always studied fully shuffled
  } else {
    deckLoaded = deck.loadFromCsv(deckPath.c_str());
  }
  cardState = CardState::Front;

  if (mode == ReviewMode::Linear) {
    // Linear never reads the clock and never writes the deck; its progress
    // lives in a separate file (FlashcardLinearProgress).
    bool restored = false;
    if (deckLoaded && resumeLinear) {
      FlashcardLinearProgress::Saved saved;
      if (FlashcardLinearProgress::load(deckPath, saved) && saved.fingerprint == linearFingerprint()) {
        restored = linear.restore(saved.state, linearDeckSize(), shuffle);
      }
      if (!restored) LOG_INF("FCRV", "Saved linear pass unusable (deck changed?), starting over: %s", deckPath.c_str());
    }
    if (!deckLoaded) {
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
  if (mode == ReviewMode::Linear && linearDirty) saveLinearProgress();
  FlashcardHistory::flush();
  Activity::onExit();
}

void FlashcardReviewActivity::saveLinearProgress() {
  if (!deckLoaded) return;
  FlashcardLinearProgress::Saved saved;
  saved.fingerprint = linearFingerprint();
  saved.state = linear.exportState();
  FlashcardHistory::flush();
  if (FlashcardLinearProgress::save(deckPath, saved)) {
    linearDirty = false;
    marksSinceSave = 0;
  }
}

// ─── Input ───────────────────────────────────────────────────────────────────

void FlashcardReviewActivity::loop() {
  if (mode == ReviewMode::Linear) {
    loopLinear();
    return;
  }

  if (pendingSave) {
    saveProgress();
    pendingSave = false;
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
    rateCard(SrsRating::Again);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
             mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    rateCard(SrsRating::Hard);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
             mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    rateCard(SrsRating::Good);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    rateCard(SrsRating::Easy);
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
        markLinear(LinearPile::Missed);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
                 mappedInput.wasReleased(MappedInputManager::Button::Up)) {
        markLinear(LinearPile::Unsure);
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
                 mappedInput.wasReleased(MappedInputManager::Button::Down)) {
        markLinear(LinearPile::Known);
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

  switch (summaryOptions[summarySelected]) {
    case SummaryOption::Repeat:
      linear.startRepeatPass(esp_random());
      linearDirty = true;
      break;
    case SummaryOption::Full:
      linear.startFullPass(esp_random());
      linearDirty = true;
      break;
    case SummaryOption::NextDeck:
      finishWithNextDeck();
      return;
    case SummaryOption::Exit:
      finish();
      return;
  }
  cardShownMs = millis();  // time on the summary screen is not study time
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
  cardState = CardState::Back;
  requestUpdate();
}

void FlashcardReviewActivity::rateCard(SrsRating rating) {
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

  if (currentQueueIndex >= reviewQueue.size()) {
    saveProgress();
    pendingSave = false;
    showDone();
  } else {
    cardState = CardState::Front;
    requestUpdate();
  }
}

// Spaced only: persist the deck and, with it, the daily new-card counter.
void FlashcardReviewActivity::saveProgress() {
  if (!deckLoaded) return;  // never overwrite a CSV we could not read
  if (unsavedRatings > 0) {
    if (deck.saveToCsv(deckPath.c_str())) {
      unsavedRatings = 0;
    } else {
      LOG_ERR("FCRV", "Failed to save %s", deckPath.c_str());
    }
  }
  if (newQuotaDirty) {
    FLASHCARD_STORE.saveToFile();
    FLASHCARD_DECK_OPTIONS.saveToFile();
    newQuotaDirty = false;
  }
  FlashcardHistory::flush();
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
  linear.mark(pile);
  linearDirty = true;
  FlashcardHistory::recordLinear(FlashcardSrs::getToday(), cardSeconds());
  // Periodic save so a flat battery loses at most a few cards of progress.
  if (++marksSinceSave >= SAVE_EVERY_RATINGS) saveLinearProgress();
  if (linear.passDone() && !wholeFolder && linear.passSize() == deck.getCards().size()) {
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
  if (deckLoaded) return nullptr;
  return deckPath == FLASHCARD_HARD_KEY ? tr(STR_FLASHCARD_NO_HARD) : tr(STR_FLASHCARD_NOT_LOADED);
}

// Seconds on the card just answered (capped by the history), and start
// timing the next one.
uint16_t FlashcardReviewActivity::cardSeconds() {
  const unsigned long now = millis();
  const unsigned long elapsed = (now - cardShownMs) / 1000;
  cardShownMs = now;
  return static_cast<uint16_t>(std::min<unsigned long>(elapsed, FlashcardHistory::MAX_CARD_SECONDS));
}

size_t FlashcardReviewActivity::linearDeckSize() const {
  return wholeFolder ? folderIndex.size() : deck.getCards().size();
}

uint32_t FlashcardReviewActivity::linearFingerprint() const {
  return wholeFolder ? folderIndex.fingerprint() : deck.contentFingerprint();
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
  summaryOptions.reserve(4);
  if (linear.canRepeat()) summaryOptions.push_back(SummaryOption::Repeat);
  summaryOptions.push_back(SummaryOption::Full);
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

// ─── Render ──────────────────────────────────────────────────────────────────

void FlashcardReviewActivity::render(RenderLock&&) {
  if (mode == ReviewMode::Linear) {
    if (cardState == CardState::Summary) {
      renderSummary();
      return;
    }
    if (linear.passDone()) return;
    renderCard(wholeFolder ? folderCard : deck.getCards()[linear.current()]);
    return;
  }
  if (reviewQueue.empty() || currentQueueIndex >= reviewQueue.size()) return;
  renderCard(deck.getCards()[reviewQueue[currentQueueIndex]]);
}

void FlashcardReviewActivity::renderCard(const FlashcardCard& card) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  renderer.clearScreen();

  char progress[32] = "";
  if (mode == ReviewMode::Linear) {
    snprintf(progress, sizeof(progress), "%s %u \xC2\xB7 %u/%u", tr(STR_FLASHCARD_PASS),
             static_cast<unsigned>(linear.pass()), static_cast<unsigned>(linear.shownPosition()),
             static_cast<unsigned>(linear.passSize()));
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, displayName().c_str(),
                 progress[0] ? progress : nullptr);

  const auto area = cardArea(renderer);
  std::string mainText, hint;
  card.parseFront(mainText, hint);

  if (cardState == CardState::Front) {
    const auto front = FlashcardLayout::layoutFront(renderer, mainText, hint, area, UI_10_FONT_ID);
    drawCenteredLines(renderer, front.mainFont, front.mainY, front.mainLines);
    drawCenteredLines(renderer, front.hintFont, front.hintY, front.hintLines);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_FLASHCARD_SHOW), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto back = FlashcardLayout::layoutBack(renderer, mainText, card.backContent, area, UI_12_FONT_ID);
    drawCenteredLines(renderer, back.questionFont, back.questionY, back.questionLines);
    renderer.drawLine(metrics.contentSidePadding, back.separatorY, pageWidth - metrics.contentSidePadding,
                      back.separatorY, true);
    drawCenteredLines(renderer, back.answerFont, back.answerY, back.answerLines);

    if (mode == ReviewMode::Linear) {
      const auto labels =
          mappedInput.mapLabels(tr(STR_FLASHCARD_MISSED), "", tr(STR_FLASHCARD_UNSURE), tr(STR_FLASHCARD_KNOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      // Rating + resulting interval. Back→Again, Confirm→Easy, Previous→Hard, Next→Good
      char again[32], easy[32], hard[32], good[32];
      snprintf(again, sizeof(again), "%s %s", tr(STR_FLASHCARD_AGAIN),
               FlashcardSrs::previewInterval(card.srs, SrsRating::Again));
      snprintf(easy, sizeof(easy), "%s %s", tr(STR_FLASHCARD_EASY),
               FlashcardSrs::previewInterval(card.srs, SrsRating::Easy));
      snprintf(hard, sizeof(hard), "%s %s", tr(STR_FLASHCARD_HARD),
               FlashcardSrs::previewInterval(card.srs, SrsRating::Hard));
      snprintf(good, sizeof(good), "%s %s", tr(STR_FLASHCARD_GOOD),
               FlashcardSrs::previewInterval(card.srs, SrsRating::Good));
      const auto labels = mappedInput.mapLabels(again, easy, hard, good);
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  }
  renderer.displayBuffer();
}

void FlashcardReviewActivity::renderSummary() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePad = metrics.contentSidePadding;

  renderer.clearScreen();
  char title[32];
  snprintf(title, sizeof(title), "%s %u", tr(STR_FLASHCARD_PASS), static_cast<unsigned>(linear.pass()));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, displayName().c_str(), title);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  renderer.drawCenteredText(UI_12_FONT_ID, y, title, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

  // Missed / Unsure / Known counts
  const size_t counts[3] = {linear.missedCount(), linear.unsureCount(), linear.knownCount()};
  const char* names[3] = {tr(STR_FLASHCARD_MISSED), tr(STR_FLASHCARD_UNSURE), tr(STR_FLASHCARD_KNOWN)};
  const int colW = (pageWidth - sidePad * 2) / 3;
  const int line10 = renderer.getLineHeight(UI_10_FONT_ID);
  for (int i = 0; i < 3; ++i) {
    char val[8];
    snprintf(val, sizeof(val), "%u", static_cast<unsigned>(counts[i]));
    const int cx = sidePad + colW * i;
    renderer.drawText(UI_10_FONT_ID, cx + (colW - renderer.getTextWidth(UI_10_FONT_ID, names[i])) / 2, y, names[i]);
    renderer.drawText(UI_12_FONT_ID, cx + (colW - renderer.getTextWidth(UI_12_FONT_ID, val, EpdFontFamily::BOLD)) / 2,
                      y + line10 + 4, val, true, EpdFontFamily::BOLD);
  }
  y += line10 + 4 + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing * 2;

  char nextLabel[96] = "";
  snprintf(nextLabel, sizeof(nextLabel), "%s: %s", tr(STR_FLASHCARD_NEXT_DECK), nextDeckName.c_str());
  FlashcardUi::Row rows[4];
  const int count = static_cast<int>(summaryOptions.size());
  for (int i = 0; i < count; ++i) {
    switch (summaryOptions[i]) {
      case SummaryOption::Repeat:
        rows[i].title = tr(STR_FLASHCARD_REPEAT_MISSED);
        break;
      case SummaryOption::Full:
        rows[i].title = tr(STR_FLASHCARD_FULL_PASS);
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
