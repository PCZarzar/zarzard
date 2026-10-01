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

// "/flashcard/LPIC 101/deck.csv" -> "LPIC 101"; "" for decks directly in /flashcard.
std::string folderOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  if (slash == std::string::npos || slash == 0) return "";
  const size_t prev = path.rfind('/', slash - 1);
  const std::string folder = path.substr(prev + 1, slash - prev - 1);
  return folder == "flashcard" ? "" : folder;
}
}  // namespace

void FlashcardFlaggedActivity::onEnter() {
  Activity::onEnter();
  loadDecks();
  buildRows();
  requestUpdate();
}

// Counts only: no card text is kept for decks that are not open.
void FlashcardFlaggedActivity::loadDecks() {
  decks = FlashcardCardActions::countFlagged(FlashcardCatalog::allDecks());
  totalMarked = 0;
  totalSuspended = 0;
  for (const auto& d : decks) {
    totalMarked += d.marked;
    totalSuspended += d.suspended;
  }
}

void FlashcardFlaggedActivity::loadDeck() {
  cards.clear();
  if (const auto* d = currentDeck()) cards = FlashcardCardActions::collectFlagged({d->path}, MAX_LISTED);
}

const FlashcardCardActions::FlaggedCount* FlashcardFlaggedActivity::currentDeck() const {
  return openDeck >= 0 && openDeck < static_cast<int>(decks.size()) ? &decks[openDeck] : nullptr;
}

void FlashcardFlaggedActivity::buildRows() {
  rows.clear();
  if (view == View::Decks) {
    if (!decks.empty()) {
      rows.push_back({Act::None, 0});  // "All decks"
      if (totalMarked > 0) rows.push_back({Act::DeleteAllMarked});
      if (totalSuspended > 0) rows.push_back({Act::ReactivateAll});
      rows.push_back({Act::None, 1});  // "Decks"
      for (int i = 0; i < static_cast<int>(decks.size()); ++i) rows.push_back({Act::OpenDeck, i});
    }
  } else if (view == View::Deck) {
    const auto* d = currentDeck();
    if (d && d->marked > 0) {
      rows.push_back({Act::DeleteMarked});
      rows.push_back({Act::UnmarkAll});
    }
    if (d && d->suspended > 0) rows.push_back({Act::ReactivateSuspended});
    rows.push_back({Act::None, 2});  // "Cards"
    for (int i = 0; i < static_cast<int>(cards.size()); ++i) rows.push_back({Act::OpenCard, i});
  } else if (view == View::CardActions) {
    rows = {{Act::ToggleMark}, {Act::ToggleSuspend}, {Act::Reset}, {Act::Delete}};
  }
  moveTo(std::clamp(selected, 0, std::max(0, static_cast<int>(rows.size()) - 1)), 1);
}

// Lands on the first selectable row from `index` in `direction`, wrapping.
void FlashcardFlaggedActivity::moveTo(int index, const int direction) {
  const int n = static_cast<int>(rows.size());
  if (n == 0) {
    selected = 0;
    return;
  }
  for (int step = 0; step < n; ++step) {
    index = ((index % n) + n) % n;
    if (rows[index].act != Act::None) {
      selected = index;
      return;
    }
    index += direction;
  }
  selected = 0;
}

void FlashcardFlaggedActivity::activate(const Act act) {
  statusMsg.clear();
  switch (act) {
    case Act::OpenDeck:
      decksSelected = selected;
      openDeck = rows[selected].index;
      loadDeck();
      view = View::Deck;
      selected = 0;
      buildRows();
      break;
    case Act::OpenCard:
      deckSelected = selected;
      openCard = rows[selected].index;
      view = View::CardActions;
      selected = 0;
      buildRows();
      break;
    case Act::DeleteAllMarked:
    case Act::DeleteMarked:
    case Act::Delete:
      pendingConfirm = act;
      confirmFrom = view;
      view = View::Confirm;
      break;
    default:
      run(act);
      break;
  }
  requestUpdate();
}

void FlashcardFlaggedActivity::run(const Act act) {
  using FlashcardCardActions::applyToFlagged;
  constexpr uint8_t MARKED = FlashcardCard::FLAG_MARKED;
  constexpr uint8_t SUSPENDED = FlashcardCard::FLAG_SUSPENDED;
  const std::string deckPath = currentDeck() ? currentDeck()->path : "";
  std::vector<std::string> all;
  all.reserve(decks.size());
  for (const auto& d : decks) all.push_back(d.path);

  size_t changed = 0;
  size_t failed = 0;
  switch (act) {
    case Act::DeleteAllMarked:
      changed = applyToFlagged(all, MARKED, FlashcardCardAction::Delete, &failed);
      break;
    case Act::ReactivateAll:
      changed = applyToFlagged(all, SUSPENDED, FlashcardCardAction::Unsuspend, &failed);
      break;
    case Act::DeleteMarked:
      changed = applyToFlagged({deckPath}, MARKED, FlashcardCardAction::Delete, &failed);
      break;
    case Act::UnmarkAll:
      changed = applyToFlagged({deckPath}, MARKED, FlashcardCardAction::Unmark, &failed);
      break;
    case Act::ReactivateSuspended:
      changed = applyToFlagged({deckPath}, SUSPENDED, FlashcardCardAction::Unsuspend, &failed);
      break;
    case Act::ToggleMark:
    case Act::ToggleSuspend:
    case Act::Reset:
    case Act::Delete: {
      if (openCard < 0 || openCard >= static_cast<int>(cards.size())) break;
      const auto& c = cards[openCard];
      FlashcardCardAction a = FlashcardCardAction::Reset;
      if (act == Act::ToggleMark) a = isMarked(c) ? FlashcardCardAction::Unmark : FlashcardCardAction::Mark;
      if (act == Act::ToggleSuspend) {
        a = isSuspended(c) ? FlashcardCardAction::Unsuspend : FlashcardCardAction::Suspend;
      }
      if (act == Act::Delete) a = FlashcardCardAction::Delete;
      changed = FlashcardCardActions::applyToFiles({{c.path, c.offset, a}});
      if (changed == 0) failed = 1;
      break;
    }
    default:
      return;
  }

  if (failed > 0) {
    statusMsg = tr(STR_ZZ_SAVE_FAILED);
  } else {
    char buf[48];
    snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(changed), tr(STR_ZZ_CARDS_CHANGED));
    statusMsg = buf;
  }

  // Record positions moved with the rewrite: count and list everything again.
  loadDecks();
  openDeck = -1;
  for (int i = 0; i < static_cast<int>(decks.size()); ++i) {
    if (decks[i].path == deckPath) openDeck = i;
  }
  if (deckPath.empty() || openDeck < 0) {
    // Everything was done at the top level, or the open deck has nothing left.
    view = View::Decks;
    selected = deckPath.empty() ? selected : decksSelected;
  } else {
    loadDeck();
    view = View::Deck;
    if (act == Act::ToggleMark || act == Act::ToggleSuspend || act == Act::Reset || act == Act::Delete) {
      selected = deckSelected;
    }
  }
  openCard = -1;
  buildRows();
}

void FlashcardFlaggedActivity::back() {
  statusMsg.clear();
  switch (view) {
    case View::Confirm:
      view = confirmFrom;
      break;
    case View::CardActions:
      view = View::Deck;
      selected = deckSelected;
      openCard = -1;
      buildRows();
      break;
    case View::Deck:
      view = View::Decks;
      selected = decksSelected;
      openDeck = -1;
      cards.clear();
      buildRows();
      break;
    case View::Decks:
      finish();
      return;
  }
  requestUpdate();
}

void FlashcardFlaggedActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    RenderLock lock(*this);
    back();
    return;
  }
  if (view == View::Confirm) {
    if (mappedInput.wasReleased(Button::Confirm)) {
      RenderLock lock(*this);
      run(pendingConfirm);
      requestUpdate();
    }
    return;
  }
  if (rows.empty()) return;
  buttonNavigator.onNext([this] {
    moveTo(selected + 1, 1);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    moveTo(selected - 1, -1);
    requestUpdate();
  });
  if (mappedInput.wasReleased(Button::Confirm)) {
    RenderLock lock(*this);
    activate(rows[selected].act);
  }
}

void FlashcardFlaggedActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  const auto* deck = currentDeck();
  const std::string deckName = deck ? FlashcardDeck::displayNameForPath(deck->path) : "";
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 view == View::Decks || (view == View::Confirm && confirmFrom == View::Decks)
                     ? tr(STR_FLASHCARD_FLAGGED_TITLE)
                     : deckName.c_str());
  int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (view == View::Confirm) {
    const int midY = pageHeight / 3;
    char title[96];
    if (pendingConfirm == Act::DeleteAllMarked) {
      snprintf(title, sizeof(title), "%s (%u)", tr(STR_ZZ_DELETE_ALL_MARKED_CONFIRM),
               static_cast<unsigned>(totalMarked));
    } else if (pendingConfirm == Act::DeleteMarked) {
      snprintf(title, sizeof(title), "%s (%u)", tr(STR_FLASHCARD_DELETE_MARKED_CONFIRM),
               static_cast<unsigned>(deck ? deck->marked : 0));
    } else {
      snprintf(title, sizeof(title), "%s", tr(STR_FLASHCARD_DELETE_CONFIRM));
    }
    const auto lines =
        renderer.wrappedText(UI_12_FONT_ID, title, pageWidth - 2 * metrics.contentSidePadding, 3, EpdFontFamily::BOLD);
    int y = midY;
    for (const auto& l : lines) {
      renderer.drawCenteredText(UI_12_FONT_ID, y, l.c_str(), true, EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_12_FONT_ID);
    }
    renderer.drawCenteredText(UI_10_FONT_ID, y + 8, tr(STR_FLASHCARD_CANNOT_UNDO));
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_DELETE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (!statusMsg.empty()) {
    renderer.drawCenteredText(SMALL_FONT_ID, top, statusMsg.c_str(), true, EpdFontFamily::BOLD);
    top += renderer.getLineHeight(SMALL_FONT_ID) + 6;
  }

  if (view == View::CardActions && openCard >= 0 && openCard < static_cast<int>(cards.size())) {
    // The card itself above its actions.
    const auto& c = cards[openCard];
    const int w = pageWidth - 2 * metrics.contentSidePadding;
    for (const auto& l : renderer.wrappedText(UI_12_FONT_ID, c.front.c_str(), w, 2, EpdFontFamily::BOLD)) {
      renderer.drawCenteredText(UI_12_FONT_ID, top, l.c_str(), true, EpdFontFamily::BOLD);
      top += renderer.getLineHeight(UI_12_FONT_ID);
    }
    for (const auto& l : renderer.wrappedText(SMALL_FONT_ID, c.back.c_str(), w, 3)) {
      renderer.drawCenteredText(SMALL_FONT_ID, top, l.c_str());
      top += renderer.getLineHeight(SMALL_FONT_ID);
    }
    top += metrics.verticalSpacing;
  }

  if (view == View::Decks && decks.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, top + 20, tr(STR_FLASHCARD_NO_FLAGGED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, top + 20 + renderer.getLineHeight(UI_12_FONT_ID) + 6,
                              tr(STR_FLASHCARD_FLAGGED_HINT));
  } else {
    std::vector<FlashcardUi::Row> list(rows.size());
    std::vector<std::string> titles(rows.size());
    std::vector<std::string> subs(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
      const Row& r = rows[i];
      char buf[112];
      switch (r.act) {
        case Act::None:
          list[i].header = true;
          if (r.index == 0) {
            titles[i] = tr(STR_ZZ_ALL_DECKS);
          } else if (r.index == 1) {
            titles[i] = tr(STR_ZZ_DECKS);
          } else {
            // Cards with both flags count once; more than MAX_LISTED show as "80/120".
            uint32_t flagged = deck ? static_cast<uint32_t>(std::max(deck->marked, deck->suspended)) : 0;
            flagged = std::max<uint32_t>(flagged, static_cast<uint32_t>(cards.size()));
            if (cards.size() < flagged) {
              snprintf(buf, sizeof(buf), "%s (%u/%u)", tr(STR_ZZ_CARDS), static_cast<unsigned>(cards.size()),
                       static_cast<unsigned>(flagged));
            } else {
              snprintf(buf, sizeof(buf), "%s (%u)", tr(STR_ZZ_CARDS), static_cast<unsigned>(cards.size()));
            }
            titles[i] = buf;
          }
          break;
        case Act::DeleteAllMarked:
          snprintf(buf, sizeof(buf), "%s (%u)", tr(STR_ZZ_DELETE_ALL_MARKED), static_cast<unsigned>(totalMarked));
          titles[i] = buf;
          break;
        case Act::ReactivateAll:
          snprintf(buf, sizeof(buf), "%s (%u)", tr(STR_ZZ_REACTIVATE_ALL), static_cast<unsigned>(totalSuspended));
          titles[i] = buf;
          break;
        case Act::OpenDeck: {
          const auto& d = decks[r.index];
          titles[i] = FlashcardDeck::displayNameForPath(d.path);
          const std::string folder = folderOf(d.path);
          snprintf(buf, sizeof(buf), "%s%s%u %s \xC2\xB7 %u %s", folder.c_str(), folder.empty() ? "" : " \xC2\xB7 ",
                   static_cast<unsigned>(d.marked), tr(STR_FLASHCARD_MARKED_LC), static_cast<unsigned>(d.suspended),
                   tr(STR_FLASHCARD_SUSPENDED_LC));
          subs[i] = buf;
          break;
        }
        case Act::DeleteMarked:
          snprintf(buf, sizeof(buf), "%s (%u)", tr(STR_FLASHCARD_DELETE_MARKED),
                   static_cast<unsigned>(deck ? deck->marked : 0));
          titles[i] = buf;
          break;
        case Act::UnmarkAll:
          snprintf(buf, sizeof(buf), "%s (%u)", tr(STR_FLASHCARD_UNMARK_ALL),
                   static_cast<unsigned>(deck ? deck->marked : 0));
          titles[i] = buf;
          break;
        case Act::ReactivateSuspended:
          snprintf(buf, sizeof(buf), "%s (%u)", tr(STR_FLASHCARD_UNSUSPEND_ALL),
                   static_cast<unsigned>(deck ? deck->suspended : 0));
          titles[i] = buf;
          break;
        case Act::OpenCard: {
          const auto& c = cards[r.index];
          titles[i] = c.front;
          if (static_cast<int>(i) == selected) {
            subs[i] = c.back;  // the selected card shows its answer
          } else {
            snprintf(buf, sizeof(buf), "%s%s%s", isMarked(c) ? tr(STR_FLASHCARD_MARKED_LC) : "",
                     isMarked(c) && isSuspended(c) ? " \xC2\xB7 " : "",
                     isSuspended(c) ? tr(STR_FLASHCARD_SUSPENDED_LC) : "");
            subs[i] = buf;
          }
          break;
        }
        case Act::ToggleMark:
          titles[i] = openCard >= 0 && isMarked(cards[openCard]) ? tr(STR_FLASHCARD_UNMARK) : tr(STR_FLASHCARD_MARK);
          break;
        case Act::ToggleSuspend:
          titles[i] =
              openCard >= 0 && isSuspended(cards[openCard]) ? tr(STR_FLASHCARD_UNSUSPEND) : tr(STR_FLASHCARD_SUSPEND);
          break;
        case Act::Reset:
          titles[i] = tr(STR_FLASHCARD_RESET_CARD);
          subs[i] = tr(STR_FLASHCARD_RESET_DESC);
          break;
        case Act::Delete:
          titles[i] = tr(STR_DELETE);
          subs[i] = tr(STR_FLASHCARD_DELETE_DESC);
          break;
        default:
          break;
      }
      list[i].title = titles[i].c_str();
      list[i].subtitle = subs[i].empty() ? nullptr : subs[i].c_str();
    }
    FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, bottom - top}, list.data(), static_cast<int>(list.size()),
                          selected);
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), rows.empty() ? "" : tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
