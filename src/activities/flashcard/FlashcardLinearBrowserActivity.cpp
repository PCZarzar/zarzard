#include "FlashcardLinearBrowserActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "FlashcardCatalog.h"
#include "FlashcardFolderIndex.h"
#include "FlashcardLinearProgress.h"
#include "FlashcardStatsScan.h"
#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FlashcardLinearBrowserActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

void FlashcardLinearBrowserActivity::reload() {
  items.clear();
  if (folder.empty()) {
    // Found when opened (reading every deck here would slow the list down).
    Item hardItem;
    hardItem.isHard = true;
    hardItem.name = tr(STR_FLASHCARD_HARD_CARDS);
    hardItem.path = FLASHCARD_HARD_KEY;
    FlashcardLinearProgress::Saved saved;
    if (FlashcardLinearProgress::load(hardItem.path, saved)) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s %u \xC2\xB7 %u/%u", tr(STR_FLASHCARD_PASS),
               static_cast<unsigned>(saved.state.pass),
               static_cast<unsigned>(std::min(saved.state.position + 1, saved.state.queue.size())),
               static_cast<unsigned>(saved.state.queue.size()));
      hardItem.savedLabel = buf;
    }
    items.push_back(std::move(hardItem));
    for (const auto& name : FlashcardDeck::listFolders()) {
      Item it;
      it.isFolder = true;
      it.name = name;
      it.folderDecks = static_cast<uint16_t>(FlashcardCatalog::decksIn(name).size());
      items.push_back(std::move(it));
    }
  }
  const auto paths = FlashcardCatalog::decksIn(folder);
  items.reserve(items.size() + paths.size());
  for (const auto& path : paths) {
    Item it;
    it.path = path;
    FlashcardCatalog::Entry e;
    it.loaded = FlashcardCatalog::load(path, 0, e);
    if (it.loaded) {
      it.name = e.name;
      it.cards = e.stats.totalCards;
    } else {
      it.name = path.substr(path.rfind('/') + 1);
    }
    FlashcardLinearProgress::Saved saved;
    if (it.loaded && FlashcardLinearProgress::load(path, saved)) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s %u \xC2\xB7 %u/%u", tr(STR_FLASHCARD_PASS),
               static_cast<unsigned>(saved.state.pass),
               static_cast<unsigned>(std::min(saved.state.position + 1, saved.state.queue.size())),
               static_cast<unsigned>(saved.state.queue.size()));
      it.savedLabel = buf;
    }
    items.push_back(std::move(it));
  }
  if (!folder.empty()) {
    Item whole;
    whole.isWholeFolder = true;
    whole.name = folder;
    whole.path = std::string(FLASHCARD_DIR) + "/" + folder + "/";
    whole.loaded = false;
    for (const auto& it : items) {
      if (it.isFolder || !it.loaded) continue;
      whole.loaded = true;
      whole.folderCards += it.cards;
    }
    whole.cards = static_cast<uint16_t>(std::min<uint32_t>(whole.folderCards, FlashcardFolderIndex::MAX_CARDS));
    FlashcardLinearProgress::Saved saved;
    if (whole.loaded && FlashcardLinearProgress::load(whole.path, saved)) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s %u \xC2\xB7 %u/%u", tr(STR_FLASHCARD_PASS),
               static_cast<unsigned>(saved.state.pass),
               static_cast<unsigned>(std::min(saved.state.position + 1, saved.state.queue.size())),
               static_cast<unsigned>(saved.state.queue.size()));
      whole.savedLabel = buf;
    }
    if (whole.loaded) items.insert(items.begin(), std::move(whole));

    // The saved Selection, then "Choose decks…", right under the whole folder.
    int loadedDecks = 0;
    for (const auto& it : items) {
      if (!it.isFolder && !it.isWholeFolder && it.loaded) ++loadedDecks;
    }
    std::vector<Item> extra;
    const std::string dir = folderDir();
    const auto chosen = FlashcardLinearProgress::loadSelection(dir);
    if (!chosen.empty()) {
      Item sel;
      sel.isSelection = true;
      sel.path = FlashcardLinearProgress::selectionKey(dir);
      for (const auto& p : chosen) {
        const auto found = std::find_if(items.begin(), items.end(), [&p](const Item& it) { return it.path == p; });
        if (!sel.name.empty()) sel.name += " + ";
        sel.name += found != items.end() ? found->name : p.substr(p.rfind('/') + 1);
        if (found != items.end()) sel.folderCards += found->cards;
      }
      sel.folderDecks = static_cast<uint16_t>(chosen.size());
      sel.cards = static_cast<uint16_t>(std::min<uint32_t>(sel.folderCards, FlashcardFolderIndex::MAX_CARDS));
      FlashcardLinearProgress::Saved saved;
      if (FlashcardLinearProgress::load(sel.path, saved)) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s %u \xC2\xB7 %u/%u", tr(STR_FLASHCARD_PASS),
                 static_cast<unsigned>(saved.state.pass),
                 static_cast<unsigned>(std::min(saved.state.position + 1, saved.state.queue.size())),
                 static_cast<unsigned>(saved.state.queue.size()));
        sel.savedLabel = buf;
      }
      extra.push_back(std::move(sel));
    }
    if (loadedDecks >= 2) {
      Item chooser;
      chooser.isChooser = true;
      chooser.name = tr(STR_ZZ_CHOOSE_DECKS);
      extra.push_back(std::move(chooser));
    }
    const size_t at = !items.empty() && items.front().isWholeFolder ? 1 : 0;
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(at), std::make_move_iterator(extra.begin()),
                 std::make_move_iterator(extra.end()));
  }
  if (selectedIndex >= static_cast<int>(items.size())) {
    selectedIndex = items.empty() ? 0 : static_cast<int>(items.size()) - 1;
  }
}

int FlashcardLinearBrowserActivity::findDeck(const std::string& path) const {
  for (size_t i = 0; i < items.size(); ++i) {
    if (!items[i].isFolder && !items[i].isWholeFolder && !items[i].isHard && !items[i].isSelection &&
        !items[i].isChooser && items[i].path == path) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

FlashcardNextDeck FlashcardLinearBrowserActivity::resolveNextDeckThunk(void* self) {
  return static_cast<FlashcardLinearBrowserActivity*>(self)->resolveNextDeck();
}

// Next readable deck of the same folder, in list order, without wrapping.
FlashcardNextDeck FlashcardLinearBrowserActivity::resolveNextDeck() {
  FlashcardNextDeck offer;
  if (currentPath == FLASHCARD_HARD_KEY || (!currentPath.empty() && currentPath.back() == '/')) {
    return offer;  // whole folder or difficult cards: no next deck
  }
  const int current = findDeck(currentPath);
  if (current < 0) return offer;
  for (size_t i = current + 1; i < items.size(); ++i) {
    if (!items[i].isFolder && !items[i].isWholeFolder && !items[i].isHard && !items[i].isSelection &&
        !items[i].isChooser && items[i].loaded) {
      offer.name = items[i].name;
      offer.cards = items[i].cards;
      return offer;
    }
  }
  return offer;
}

void FlashcardLinearBrowserActivity::startReview(const int itemIndex, const bool resume) {
  currentPath = items[itemIndex].path;
  startActivityForResult(
      std::make_unique<FlashcardReviewActivity>(
          renderer, mappedInput, currentPath, FlashcardReviewActivity::ReviewMode::Linear, FLASHCARD_STORE.shuffle != 0,
          resume, FlashcardNextDeckProvider{this, &resolveNextDeckThunk}),
      [this](const ActivityResult& result) {
        RenderLock lock(*this);  // result handlers run unlocked; reload() rebuilds the list render() reads
        const int current = findDeck(currentPath);
        const auto* menu = std::get_if<MenuResult>(&result.data);
        const bool wantsNext = menu && menu->action == FlashcardReviewActivity::RESULT_NEXT_DECK;
        reload();  // saved-pass labels changed
        const int cur = findDeck(currentPath) >= 0 ? findDeck(currentPath) : current;
        if (wantsNext && cur >= 0) {
          for (size_t i = cur + 1; i < items.size(); ++i) {
            if (items[i].isFolder || items[i].isWholeFolder || items[i].isHard || !items[i].loaded) continue;
            selectedIndex = static_cast<int>(i);
            // A chained deck picks up its own saved pass, if any.
            startReview(selectedIndex, !items[i].savedLabel.empty());
            return;
          }
        }
        if (cur >= 0) selectedIndex = cur;
        // Whole-folder and difficult-cards rows are the first row of their list.
        if (currentPath == FLASHCARD_HARD_KEY || (!currentPath.empty() && currentPath.back() == '/')) {
          selectedIndex = 0;
        }
        for (size_t i = 0; i < items.size(); ++i) {
          if (items[i].isSelection && items[i].path == currentPath) selectedIndex = static_cast<int>(i);
        }
        requestUpdate();
      });
}

void FlashcardLinearBrowserActivity::loop() {
  if (choosingDecks) {
    loopChooser();
    return;
  }
  if (choosingStart) {
    buttonNavigator.onNext([this] {
      startChoice = ButtonNavigator::nextIndex(startChoice, 2);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      startChoice = ButtonNavigator::previousIndex(startChoice, 2);
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      choosingStart = false;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      choosingStart = false;
      startReview(selectedIndex, startChoice == 0);
    }
    return;
  }

  const int count = static_cast<int>(items.size());
  if (count > 0) {
    buttonNavigator.onNext([this, count] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, count] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
      requestUpdate();
    });
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!folder.empty()) {
      RenderLock lock(*this);  // reload() rebuilds the list render() reads
      // Up to /flashcard, with the folder we came from selected.
      const std::string from = folder;
      folder.clear();
      reload();
      for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].isFolder && items[i].name == from) selectedIndex = static_cast<int>(i);
      }
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (count == 0 || !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;

  const Item& it = items[selectedIndex];
  if (it.isFolder) {
    RenderLock lock(*this);  // reload() rebuilds the list render() reads
    folder = it.name;
    selectedIndex = 0;
    reload();
    requestUpdate();
    return;
  }
  if (it.isChooser) {
    RenderLock lock(*this);
    openChooser();
    return;
  }
  if (!it.loaded) return;
  if (!it.savedLabel.empty()) {
    choosingStart = true;
    startChoice = 0;
    requestUpdate();
    return;
  }
  startReview(selectedIndex, false);
}

void FlashcardLinearBrowserActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  const char* title = choosingDecks    ? tr(STR_ZZ_CHOOSE_DECKS)
                      : choosingStart  ? items[selectedIndex].name.c_str()
                      : folder.empty() ? tr(STR_FLASHCARD_LINEAR)
                                       : folder.c_str();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  if (choosingDecks) {
    renderChooser(top, height);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (choosingStart) {
    FlashcardUi::Row choice[2];
    choice[0].title = tr(STR_FLASHCARD_CONTINUE);
    choice[0].subtitle = items[selectedIndex].savedLabel.c_str();
    choice[1].title = tr(STR_FLASHCARD_RESTART);
    FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, choice, 2, startChoice);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (items.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, top + 10, tr(STR_FLASHCARD_NO_DECKS), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + 10 + renderer.getLineHeight(UI_12_FONT_ID) + 6,
                              tr(STR_FLASHCARD_IMPORT_HINT));
  }

  // Row text lives in these vectors while drawRows runs.
  std::vector<FlashcardUi::Row> rows(items.size());
  std::vector<std::string> titles(items.size());
  std::vector<std::string> subs(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    const Item& it = items[i];
    char buf[96];
    if (it.isHard) {
      titles[i] = it.name;
      if (it.savedLabel.empty()) {
        snprintf(buf, sizeof(buf), "%s", tr(STR_FLASHCARD_HARD_HINT));
      } else {
        snprintf(buf, sizeof(buf), "%s %s", tr(STR_FLASHCARD_SAVED), it.savedLabel.c_str());
      }
    } else if (it.isWholeFolder) {
      titles[i] = tr(STR_FLASHCARD_WHOLE_FOLDER);
      const int n = snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(it.cards), tr(STR_FLASHCARD_CARDS));
      if (it.folderCards > FlashcardFolderIndex::MAX_CARDS && n > 0 && n < static_cast<int>(sizeof(buf))) {
        snprintf(buf + n, sizeof(buf) - n, " (%s %u)", tr(STR_FLASHCARD_OF), static_cast<unsigned>(it.folderCards));
      }
      if (!it.savedLabel.empty()) {
        const size_t used = strlen(buf);
        snprintf(buf + used, sizeof(buf) - used, " \xC2\xB7 %s %s", tr(STR_FLASHCARD_SAVED), it.savedLabel.c_str());
      }
    } else if (it.isSelection) {
      titles[i] = std::string(tr(STR_ZZ_SELECTION)) + ": " + it.name;
      const int n = snprintf(buf, sizeof(buf), "%u %s \xC2\xB7 %u %s", static_cast<unsigned>(it.folderDecks),
                             tr(STR_FLASHCARD_DECKS), static_cast<unsigned>(it.cards), tr(STR_FLASHCARD_CARDS));
      if (!it.savedLabel.empty() && n > 0 && n < static_cast<int>(sizeof(buf))) {
        snprintf(buf + n, sizeof(buf) - n, " \xC2\xB7 %s %s", tr(STR_FLASHCARD_SAVED), it.savedLabel.c_str());
      }
    } else if (it.isChooser) {
      titles[i] = it.name;
      snprintf(buf, sizeof(buf), "%s", tr(STR_ZZ_CHOOSE_DECKS_HINT));
    } else if (it.isFolder) {
      titles[i] = it.name + "/";
      snprintf(buf, sizeof(buf), "%s \xC2\xB7 %u %s", tr(STR_FLASHCARD_FOLDER), static_cast<unsigned>(it.folderDecks),
               tr(STR_FLASHCARD_DECKS));
    } else {
      titles[i] = it.name;
      if (!it.loaded) {
        snprintf(buf, sizeof(buf), "%s", tr(STR_FLASHCARD_NOT_LOADED));
      } else if (!it.savedLabel.empty()) {
        snprintf(buf, sizeof(buf), "%u %s \xC2\xB7 %s %s", static_cast<unsigned>(it.cards), tr(STR_FLASHCARD_CARDS),
                 tr(STR_FLASHCARD_SAVED), it.savedLabel.c_str());
      } else {
        snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(it.cards), tr(STR_FLASHCARD_CARDS));
      }
    }
    subs[i] = buf;
    rows[i].title = titles[i].c_str();
    rows[i].subtitle = subs[i].c_str();
  }
  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, rows.data(), static_cast<int>(rows.size()),
                        selectedIndex);

  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

// ─── Choose decks ────────────────────────────────────────────────────────────

std::string FlashcardLinearBrowserActivity::folderDir() const { return std::string(FLASHCARD_DIR) + "/" + folder; }

// The folder's readable decks, ticked as in the saved Selection.
void FlashcardLinearBrowserActivity::openChooser() {
  pickable.clear();
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    const Item& it = items[i];
    if (!it.isFolder && !it.isWholeFolder && !it.isHard && !it.isSelection && !it.isChooser && it.loaded) {
      pickable.push_back(i);
    }
  }
  const auto chosen = FlashcardLinearProgress::loadSelection(folderDir());
  picked.assign(pickable.size(), false);
  for (size_t k = 0; k < pickable.size(); ++k) {
    picked[k] = std::find(chosen.begin(), chosen.end(), items[pickable[k]].path) != chosen.end();
  }
  pickSelected = 0;
  choosingDecks = true;
  requestUpdate();
}

void FlashcardLinearBrowserActivity::loopChooser() {
  const int count = static_cast<int>(pickable.size()) + 1;  // row 0 starts
  buttonNavigator.onNext([this, count] {
    pickSelected = ButtonNavigator::nextIndex(pickSelected, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    pickSelected = ButtonNavigator::previousIndex(pickSelected, count);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    choosingDecks = false;
    requestUpdate();
    return;
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return;
  if (pickSelected > 0) {
    picked[pickSelected - 1] = !picked[pickSelected - 1];
    requestUpdate();
    return;
  }
  if (std::find(picked.begin(), picked.end(), true) == picked.end()) return;  // nothing ticked yet
  startSelection();
}

// Saves the ticked decks (in list order) as the folder's Selection and starts
// a new pass over them.
void FlashcardLinearBrowserActivity::startSelection() {
  std::vector<std::string> paths;
  for (size_t k = 0; k < pickable.size(); ++k) {
    if (picked[k]) paths.push_back(items[pickable[k]].path);
  }
  const std::string dir = folderDir();
  if (!FlashcardLinearProgress::saveSelection(dir, paths)) return;
  RenderLock lock(*this);  // reload() rebuilds the list render() reads
  choosingDecks = false;
  const std::string key = FlashcardLinearProgress::selectionKey(dir);
  reload();
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i].isSelection && items[i].path == key) {
      selectedIndex = static_cast<int>(i);
      startReview(selectedIndex, false);  // a new choice starts a new pass
      return;
    }
  }
  requestUpdate();
}

void FlashcardLinearBrowserActivity::renderChooser(const int top, const int height) {
  const int pageWidth = renderer.getScreenWidth();
  uint32_t decks = 0, cards = 0;
  for (size_t k = 0; k < pickable.size(); ++k) {
    if (!picked[k]) continue;
    ++decks;
    cards += items[pickable[k]].cards;
  }
  std::vector<FlashcardUi::Row> rows(pickable.size() + 1);
  char start[64];
  if (decks > 0) {
    snprintf(start, sizeof(start), "%u %s \xC2\xB7 %u %s", static_cast<unsigned>(decks), tr(STR_FLASHCARD_DECKS),
             static_cast<unsigned>(cards), tr(STR_FLASHCARD_CARDS));
  } else {
    snprintf(start, sizeof(start), "%s", tr(STR_ZZ_PICK_DECKS));
  }
  rows[0].title = tr(STR_ZZ_START);
  rows[0].subtitle = start;
  std::vector<std::string> subs(pickable.size());
  for (size_t k = 0; k < pickable.size(); ++k) {
    const Item& it = items[pickable[k]];
    subs[k] = std::to_string(it.cards) + " " + tr(STR_FLASHCARD_CARDS);
    rows[k + 1].title = it.name.c_str();
    rows[k + 1].subtitle = subs[k].c_str();
    rows[k + 1].check = picked[k] ? 1 : 0;
  }
  FlashcardUi::drawRows(renderer, Rect{0, top, pageWidth, height}, rows.data(), static_cast<int>(rows.size()),
                        pickSelected);
}
