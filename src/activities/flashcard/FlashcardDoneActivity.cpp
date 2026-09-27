#include "FlashcardDoneActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FlashcardDoneActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void FlashcardDoneActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!next.name.empty()) {
      MenuResult r;
      r.action = FlashcardReviewActivity::RESULT_NEXT_DECK;
      setResult(std::move(r));
    }
    finish();
  }
}

void FlashcardDoneActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePad = metrics.contentSidePadding;
  const int contentW = pageWidth - sidePad * 2;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLASHCARDS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);

  // Block: message, rating counts (label over value), next-deck line
  const int blockH =
      lineH10 + metrics.verticalSpacing * 2 + lineH10 + 4 + lineH12 + metrics.verticalSpacing * 3 + lineH12;
  int y = contentTop + std::max(0, (contentHeight - blockH) / 2);

  const std::string msg = renderer.truncatedText(
      UI_10_FONT_ID, deckNotLoaded ? tr(STR_FLASHCARD_NOT_LOADED) : tr(STR_FLASHCARD_DONE_MSG), contentW);
  renderer.drawCenteredText(UI_10_FONT_ID, y, msg.c_str());
  y += lineH10 + metrics.verticalSpacing * 2;

  // Session summary: Again / Hard / Good / Easy
  const char* names[4] = {tr(STR_FLASHCARD_AGAIN), tr(STR_FLASHCARD_HARD), tr(STR_FLASHCARD_GOOD),
                          tr(STR_FLASHCARD_EASY)};
  const int colW = contentW / 4;
  for (int i = 0; i < 4; ++i) {
    char val[8];
    snprintf(val, sizeof(val), "%u", static_cast<unsigned>(ratingCounts[i]));
    const int cx = sidePad + colW * i;
    renderer.drawText(UI_10_FONT_ID, cx + (colW - renderer.getTextWidth(UI_10_FONT_ID, names[i])) / 2, y, names[i]);
    renderer.drawText(UI_12_FONT_ID, cx + (colW - renderer.getTextWidth(UI_12_FONT_ID, val, EpdFontFamily::BOLD)) / 2,
                      y + lineH10 + 4, val, true, EpdFontFamily::BOLD);
  }
  y += lineH10 + 4 + lineH12 + metrics.verticalSpacing * 3;

  if (next.name.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_FLASHCARD_ALL_CAUGHT_UP), true, EpdFontFamily::BOLD);
  } else {
    char line[128];
    snprintf(line, sizeof(line), "%s: %s (%u %s)", tr(STR_FLASHCARD_NEXT), next.name.c_str(),
             static_cast<unsigned>(next.cards), tr(STR_FLASHCARD_CARDS));
    const std::string fitted = renderer.truncatedText(UI_12_FONT_ID, line, contentW, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_12_FONT_ID, y, fitted.c_str(), true, EpdFontFamily::BOLD);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), next.name.empty() ? "" : tr(STR_FLASHCARD_NEXT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
