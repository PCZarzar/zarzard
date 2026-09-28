#pragma once
#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// "Today's date": the X4 forgets the date every time it sleeps, so after a
// wake the day is confirmed by hand (offered: the last day the date was
// known; Left/Right change it, never before that day) or synced over Wi-Fi.
class FlashcardDateActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  uint32_t day = 0;
  uint32_t minDay = 0;
  int option = 0;  // 0 = confirm this date, 1 = sync over Wi-Fi

  void changeDay(int delta);

 public:
  explicit FlashcardDateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardDate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
