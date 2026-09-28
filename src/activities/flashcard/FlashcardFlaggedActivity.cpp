#include "FlashcardFlaggedActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "FlashcardCatalog.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
bool isMarked(const FlashcardCardActions::Flagged& c) { return c.flags & FlashcardCard::FLAG_MARKED; }
bool isSuspended(const FlashcardCardActions::Flagged& c) { return c.flags & FlashcardCard::FLAG_SUSPENDED; }
}  // namespace

void FlashcardFlaggedActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

void FlashcardFlaggedActivity::reload() {
  cards = FlashcardCardActions::collectFlagged(FlashcardCatalog::allDecks(), MAX_LISTED);
  items.clear();
  items.reserve(cards.size() + 8);
  for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
    const auto& c = cards[i];
    if (items.empty() || items.back().path != c.path) {
      // A deck row before its cards (found again below once all are counted).
      Item deck;
      deck.isDeck = true;
      deck.path = c.path;
      items.push_back(deck);
    }
    Item card;
    card.path = c.path;
    card.card = i;
    items.push_back(card);
  }
  for (auto& it : items) {
    if (!it.isDeck) continue;
    for (const auto& c : cards) {
      if (c.path != it.path) continue;
      if (isMarked(c)) ++it.marked;
      if (isSuspended(c)) ++it.suspended;
    }
  }
  if (selected >= static_cast<int>(items.size())) selected = items.empty() ? 0 : static_cast<int>(items.size()) - 1;
}

void FlashcardFlaggedActivity::openActions() {
  const Item& it = items[selected];
  acts.clear();
  if (it.isDeck) {
    if (it.marked > 0) acts.push_back(Act::DeleteMarked);
    if (it.suspended > 0) acts.push_back(Act::UnsuspendAll);
    if (it.marked > 0) acts.push_back(Act::UnmarkAll);
  } else {
    acts = {Act::ToggleMark, Act::ToggleSuspend, Act::Reset, Act::Delete};
  }
  if (acts.empty()) return;
  actSelected = 0;
  view = View::Actions;
  requestUpdate();
}

void FlashcardFlaggedActivity::run(const Act act) {
  const Item& it = items[selected];
  std::vector<FlashcardCardActions::Pending> todo;
  if (it.isDeck) {
    for (const auto& c : cards) {
      if (c.path != it.path) continue;
      if (act == Act::DeleteMarked && isMarked(c)) todo.push_back({c.path, c.offset, FlashcardCardAction::Delete});
      if (act == Act::UnsuspendAll && isSuspended(c)) {
        todo.push_back({c.path, c.offset, FlashcardCardAction::Unsuspend});
      }
      if (act == Act::UnmarkAll && isMarked(c)) todo.push_back({c.path, c.offset, FlashcardCardAction::Unmark});
    }
  } else {
    const auto& c = cards[it.card];
    FlashcardCardAction a = FlashcardCardAction::Reset;
    switch (act) {
      case Act::ToggleMark:
        a = isMarked(c) ? FlashcardCardAction::Unmark : FlashcardCardAction::Mark;
        break;
      case Act::ToggleSuspend:
        a = isSuspended(c) ? FlashcardCardAction::Unsuspend : FlashcardCardAction::Suspend;
        break;
      case Act::Delete:
        a = FlashcardCardAction::Delete;
        break;
      default:
        break;
    }
    todo.push_back({c.path, c.offset, a});
  }
  FlashcardCardActions::applyToFiles(todo);
  // Record positions changed with the rewrite: list the cards again.
  reload();
  view = View::List;
  requestUpdate();
}

void FlashcardFlaggedActivity::loop() {
  using Button = MappedInputManager::Button;
  if (view == View::Confirm) {
    if (mappedInput.wasReleased(Button::Back)) {
      view = View::Actions;
      requestUpdate();
    } else if (mappedInput.wasReleased(Button::Confirm)) {
      RenderLock lock(*this);
      run(pendingConfirm);
    }
    return;
  }
  const bool actions = view == View::Actions;
  const int count = actions ? static_cast<int>(acts.size()) : static_cast<int>(items.size());
  if (mappedInput.wasReleased(Button::Back)) {
    if (actions) {
      view = View::List;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (count == 0) return;
  int& cursor = actions ? actSelected : selected;
  buttonNavigator.onNext([this, &cursor, count] {
    cursor = ButtonNavigator::nextIndex(cursor, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, &cursor, count] {
    cursor = ButtonNavigator::previousIndex(cursor, count);
    requestUpdate();
  });
  if (!mappedInput.wasReleased(Button::Confirm)) return;
  if (!actions) {
    openActions();
    return;
  }
  const Act act = acts[actSelected];
  if (act == Act::Delete || act == Act::DeleteMarked) {
    pendingConfirm = act;
    view = View::Confirm;
    requestUpdate();
    return;
  }
  RenderLock lock(*this);
  run(act);
}

void FlashcardFlaggedActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_FLASHCARD_FLAGGED_TITLE));
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (view == View::Confirm) {
    const int midY = pageHeight / 3;
    char title[64];
    if (pendingConfirm == Act::DeleteMarked) {
      snprintf(title, sizeof(title), "%s (%u)", tr(STR_FLASHCARD_DELETE_MARKED_CONFIRM),
               static_cast<unsigned>(items[selected].marked));
    } else {
      snprintf(title, sizeof(title), "%s", tr(STR_FLASHCARD_DELETE_CONFIRM));
    }
    renderer.drawCenteredText(UI_12_FONT_ID, midY, title, true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + renderer.getLineHeight(UI_12_FONT_ID) + 8,
                              tr(STR_FLASHCARD_CANNOT_UNDO));
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_DELETE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
  if (view == View::Actions) {
    renderActions(top, bottom);
  } else {
    renderList(top, bottom);
  }
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), items.empty() ? "" : tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void FlashcardFlaggedActivity::renderList(const int top, const int bottom) {
  const int pageWidth = renderer.getScreenWidth();
  if (items.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, top + 20, tr(STR_FLASHCARD_NO_FLAGGED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, top + 20 + renderer.getLineHeight(UI_12_FONT_ID) + 6,
                              tr(STR_FLASHCARD_FLAGGED_HINT));
    return;
  }
  std::vector<FlashcardUi::Row> rows(items.size());
  std::vector<std::string> titles(items.size());
  std::vector<std::string> subs(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    const Item& it = items[i];
    char buf[96];
    if (it.isDeck) {
      titles[i] = FlashcardDeck::displayNameForPath(it.path);
      snprintf(buf, sizeof(buf), "%u %s \xC2\xB7 %u %s", static_cast<unsigned>(it.marked), tr(STR_FLASHCARD_MARKED_LC),
               static_cast<unsigned>(it.suspended), tr(STR_FLASHCARD_SUSPENDED_LC));
      subs[i] = buf;
    } else {
      const auto& c = cards[it.card];
      titles[i] = "   " + c.front;
      if (static_cast<int>(i) == selected) {
        subs[i] = c.back;  // the selected card shows its answer
      } else {
        snprintf(buf, sizeof(buf), "%s%s%s", isMarked(c) ? tr(STR_FLASHCARD_MARKED_LC) : "",
                 isMarked(c) && isSuspended(c) ? " \xC2\xB7 " : "",
                 isSuspended(c) ? tr(STR_FLASHCARD_SUSPENDED_LC) : "");
        subs[i] = buf;
      }
    }
    rows[i].title = titles[i].c_str();
    rows[i].subtitle = subs[i].c_str();
  }
  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, bottom - top}, rows.data(), static_cast<int>(rows.size()),
                        selected);
}

void FlashcardFlaggedActivity::renderActions(const int top, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const Item& it = items[selected];
  const std::string what = it.isDeck ? FlashcardDeck::displayNameForPath(it.path) : cards[it.card].front;
  const std::string line =
      renderer.truncatedText(UI_12_FONT_ID, what.c_str(), pageWidth - metrics.contentSidePadding * 2);
  renderer.drawCenteredText(UI_12_FONT_ID, top, line.c_str(), true, EpdFontFamily::BOLD);
  const int y = top + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;

  FlashcardUi::Row rows[4];
  char labels[4][64];
  const int n = static_cast<int>(std::min<size_t>(acts.size(), 4));
  for (int i = 0; i < n; ++i) {
    labels[i][0] = '\0';
    switch (acts[i]) {
      case Act::ToggleMark:
        rows[i].title = isMarked(cards[it.card]) ? tr(STR_FLASHCARD_UNMARK) : tr(STR_FLASHCARD_MARK);
        break;
      case Act::ToggleSuspend:
        rows[i].title = isSuspended(cards[it.card]) ? tr(STR_FLASHCARD_UNSUSPEND) : tr(STR_FLASHCARD_SUSPEND);
        break;
      case Act::Reset:
        rows[i].title = tr(STR_FLASHCARD_RESET_CARD);
        rows[i].subtitle = tr(STR_FLASHCARD_RESET_DESC);
        break;
      case Act::Delete:
        rows[i].title = tr(STR_DELETE);
        rows[i].subtitle = tr(STR_FLASHCARD_DELETE_DESC);
        break;
      case Act::DeleteMarked:
        snprintf(labels[i], sizeof(labels[i]), "%s (%u)", tr(STR_FLASHCARD_DELETE_MARKED),
                 static_cast<unsigned>(it.marked));
        rows[i].title = labels[i];
        break;
      case Act::UnsuspendAll:
        snprintf(labels[i], sizeof(labels[i]), "%s (%u)", tr(STR_FLASHCARD_UNSUSPEND_ALL),
                 static_cast<unsigned>(it.suspended));
        rows[i].title = labels[i];
        break;
      case Act::UnmarkAll:
        snprintf(labels[i], sizeof(labels[i]), "%s (%u)", tr(STR_FLASHCARD_UNMARK_ALL),
                 static_cast<unsigned>(it.marked));
        rows[i].title = labels[i];
        break;
    }
  }
  FlashcardUi::drawRows(renderer, Rect{0, y, pageWidth, bottom - y}, rows, n, actSelected);
}
