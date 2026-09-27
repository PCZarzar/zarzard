#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "FlashcardHistory.h"
#include "FlashcardPeriods.h"
#include "FlashcardPlanner.h"
#include "FlashcardStatsScan.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Statistics: a panel for Today / Week / Month (Left/Right), with the study
// volume of both modes, streak, Spaced new cards and retention and a small
// chart; the side buttons pick History (bar chart by days, weeks or months),
// Forecast, Decks (phases and last Linear pass) or Difficult cards, which can
// be studied as one Linear session.
class FlashcardStatsActivity final : public Activity {
  enum class View : uint8_t { Panel, History, Forecast, Decks, Hard };
  enum class Period : uint8_t { Today, Week, Month };

  struct DeckRow {
    std::string name;
    std::string folder;
    FlashcardDeckState state = FlashcardDeckState::StandBy;
    FlashcardPhases phases;
    bool loaded = false;
    bool truncated = false;
    uint16_t linearPasses = 0;
    int lastKnownPct = -1;
  };

  ButtonNavigator buttonNavigator;
  View view = View::Panel;
  Period period = Period::Today;
  int menuIndex = 0;
  int listIndex = 0;
  bool loading = true;
  uint32_t today = 0;

  // Panel
  std::vector<FlashcardDayRecord> recent;  // from the first day shown by any period to today
  FlashcardHistory::Summary summary;

  // From the deck CSVs
  std::vector<DeckRow> decks;
  FlashcardPhases phases;  // decks not in stand-by
  uint32_t forecast[FlashcardStatsScan::FORECAST_DAYS] = {};
  std::vector<FlashcardHardCard> hard;

  // History
  FlashcardPeriods::Scale scale = FlashcardPeriods::Scale::Days;
  int pagesBack = 0;
  std::vector<FlashcardPeriods::Bucket> buckets;

  void load();
  void loadHistory();
  bool canGoOlder() const;
  void studyHard();
  void periodRange(uint32_t& from, uint32_t& to) const;

  void loopPanel();
  void loopHistory();
  void loopList(int count);

  void renderPanel(int top, int bottom);
  void renderHistory(int top, int bottom);
  void renderForecast(int top, int bottom);
  void renderDecks(int top, int bottom);
  void renderHard(int top, int bottom);

 public:
  explicit FlashcardStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlashcardStats", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
