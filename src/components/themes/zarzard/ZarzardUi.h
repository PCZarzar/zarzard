#pragma once

#include "components/icons/zarzardIcons.h"

class GfxRenderer;

// Drawing pieces shared by the Zarzard screens (Home, Flashcards).
namespace ZarzardUi {

constexpr int GUTTER = 24;
constexpr int CARD_RADIUS = 20;

void drawIcon(const GfxRenderer& renderer, const ZarzardIcons::Icon& icon, int x, int y, bool black = true);
void drawPicture(const GfxRenderer& renderer, const ZarzardIcons::Picture& picture, int x, int y, bool black = true);
// The boot and sleep mark (戦え, rules, the spaced name), centered.
void drawMark(const GfxRenderer& renderer, bool black = true);
// Rounded card: filled black when selected, 2 px outline otherwise.
void drawCard(const GfxRenderer& renderer, int x, int y, int w, int h, bool selected, int radius = CARD_RADIUS);
// Rounded progress bar; `black` false draws it white (inside a selected card).
void drawBar(const GfxRenderer& renderer, int x, int y, int w, int percent, bool black = true, int h = 8);

}  // namespace ZarzardUi
