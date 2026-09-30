#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/themes/zarzard/ZarzardUi.h"
#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  ZarzardUi::drawMark(renderer);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
