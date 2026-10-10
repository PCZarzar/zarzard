#pragma once

#include <cstdint>
#include <string>

#include "components/icons/zarzardIcons.h"

class GfxRenderer;

// Flashcard screens in the Zarzard style: the Flashcards hub, the card header
// and frame, and the big buttons over the front keys during a review.
namespace ZarzardUi {

// Horizontal centers of the four front buttons (X3 spaces them wider).
void frontButtonCenters(const GfxRenderer& renderer, int out[4]);

// A button shows either an icon or a label (with an optional second line);
// neither: no button on that key.
struct ActionButton {
  const ZarzardIcons::Icon* icon = nullptr;
  const char* label = "";
  const char* sub = "";  // second line (next interval), may be empty
};
constexpr int ACTION_BUTTON_HEIGHT = 48;
// Low outlined buttons bottom-aligned over the front keys.
int actionButtonsTop(const GfxRenderer& renderer);
void drawActionButtons(const GfxRenderer& renderer, const ActionButton buttons[4]);

// Deck name left, progress text right, a thin progress bar below. Returns the
// first free y under it.
int drawCardHeader(const GfxRenderer& renderer, const char* title, const char* right, int percent);

// The card frame runs close to the screen edges so the text gets the room.
constexpr int CARD_FRAME_GUTTER = 10;
constexpr int CARD_TEXT_INSET = 14;  // text inset inside the frame
void drawCardFrame(const GfxRenderer& renderer, int top, int bottom);

struct FlashcardHubView {
  int selected = 0;  // 0 Spaced, 1 Linear, 2 Statistics, 3 Settings
  const char* title = "";
  const char* spacedLabel = "";
  const char* linearLabel = "";
  const char* statsLabel = "";
  const char* settingsLabel = "";
  int cardsToday = -1;  // -1: date unknown
  const char* dueLabel = "";
  const char* dateUnknownLabel = "";
  uint16_t week[7] = {};  // cards per day, oldest first, today last
  uint32_t weekTotal = 0;
  const char* weekLabel = "";
  int batteryPercent = 0;
  bool showBatteryPercent = true;
};
void drawFlashcardHub(const GfxRenderer& renderer, const FlashcardHubView& view);

// Battery icon with its percentage, right-aligned at `right`.
void drawBattery(const GfxRenderer& renderer, int right, int y, int percent, bool showText);

}  // namespace ZarzardUi
