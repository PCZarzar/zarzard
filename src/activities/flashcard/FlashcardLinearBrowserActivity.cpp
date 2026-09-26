#include "FlashcardLinearBrowserActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "FlashcardCatalog.h"
#include "FlashcardLinearProgress.h"
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
  if (selectedIndex >= static_cast<int>(items.size())) selectedIndex = items.empty() ? 0 : items.size() - 1;
}

int FlashcardLinearBrowserActivity::findDeck(const std::string& path) const {
  for (size_t i = 0; i < items.size(); ++i) {
    if (!items[i].isFolder && items[i].path == path) return static_cast<int>(i);
  }
  return -1;
}

FlashcardNextDeck FlashcardLinearBrowserActivity::resolveNextDeckThunk(void* self) {
  return static_cast<FlashcardLinearBrowserActivity*>(self)->resolveNextDeck();
}

// Next readable deck of the same folder, in list order, without wrapping.
FlashcardNextDeck FlashcardLinearBrowserActivity::resolveNextDeck() {
  FlashcardNextDeck offer;
  const int current = findDeck(currentPath);
  if (current < 0) return offer;
  for (size_t i = current + 1; i < items.size(); ++i) {
    if (!items[i].isFolder && items[i].loaded) {
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
        const int current = findDeck(currentPath);
        const auto* menu = std::get_if<MenuResult>(&result.data);
        const bool wantsNext = menu && menu->action == FlashcardReviewActivity::RESULT_NEXT_DECK;
        reload();  // saved-pass labels changed
        const int cur = findDeck(currentPath) >= 0 ? findDeck(currentPath) : current;
        if (wantsNext && cur >= 0) {
          for (size_t i = cur + 1; i < items.size(); ++i) {
            if (items[i].isFolder || !items[i].loaded) continue;
            selectedIndex = static_cast<int>(i);
            // A chained deck picks up its own saved pass, if any.
            startReview(selectedIndex, !items[i].savedLabel.empty());
            return;
          }
        }
        if (cur >= 0) selectedIndex = cur;
        requestUpdate();
      });
}

void FlashcardLinearBrowserActivity::loop() {
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
    folder = it.name;
    selectedIndex = 0;
    reload();
    requestUpdate();
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
  const char* title = choosingStart    ? items[selectedIndex].name.c_str()
                      : folder.empty() ? tr(STR_FLASHCARD_LINEAR)
                                       : folder.c_str();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));

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
    if (it.isFolder) {
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
