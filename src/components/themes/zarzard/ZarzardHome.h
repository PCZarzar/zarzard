#pragma once

#include <string>
#include <vector>

#include "components/icons/zarzardIcons.h"

class GfxRenderer;

// What the Zarzard Home shows; HomeActivity fills it, drawHome lays it out.
struct ZarzardHomeTile {
  const ZarzardIcons::Icon* icon;
  const char* label;
  bool selected;
};

struct ZarzardHomeView {
  // Study card.
  const char* studyTitle = "";
  bool studySelected = false;
  int cardsToday = -1;  // -1: today's date is not known yet
  const char* cardsLabel = "";
  const char* dueLabel = "";
  const char* allDoneLabel = "";
  const char* dateUnknownLabel = "";
  std::string streakText;
  int donePercent = 0;
  // Continue-reading card (hidden without a recent book).
  bool hasBook = false;
  bool bookSelected = false;
  const char* continueLabel = "";
  std::string bookTitle;
  std::string bookAuthor;
  // The other entries, three per row.
  std::vector<ZarzardHomeTile> tiles;
  int batteryPercent = 0;
  bool showBatteryPercent = true;
};

namespace ZarzardUi {
void drawHome(const GfxRenderer& renderer, const ZarzardHomeView& view);
}
