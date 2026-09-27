#include "FlashcardStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "FlashcardCatalog.h"
#include "FlashcardChart.h"
#include "FlashcardClock.h"
#include "FlashcardDeckOptions.h"
#include "FlashcardReviewActivity.h"
#include "FlashcardSrs.h"
#include "FlashcardStore.h"
#include "FlashcardUi.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

using FlashcardPeriods::Scale;

namespace {
constexpr int MENU_HISTORY = 0;
constexpr int MENU_FORECAST = 1;
constexpr int MENU_DECKS = 2;
constexpr int MENU_HARD = 3;
constexpr int MENU_COUNT = 4;
constexpr int GAP = 8;

const char* weekdayName(const int w) {
  switch (w) {
    case 0:
      return tr(STR_SUNDAY);
    case 1:
      return tr(STR_MONDAY);
    case 2:
      return tr(STR_TUESDAY);
    case 3:
      return tr(STR_WEDNESDAY);
    case 4:
      return tr(STR_THURSDAY);
    case 5:
      return tr(STR_FRIDAY);
    default:
      return tr(STR_SATURDAY);
  }
}

// First character (UTF-8) of a weekday name: "S", "T", "Q"...
void weekdayInitial(const uint32_t day, char* out, const size_t size) {
  const char* name = weekdayName(FlashcardClock::weekday(day));
  size_t n = name[0] ? 1 : 0;
  while (name[n] && (static_cast<unsigned char>(name[n]) & 0xC0) == 0x80) ++n;
  snprintf(out, size, "%.*s", static_cast<int>(n), name);
}

void dayMonth(const uint32_t day, char* out, const size_t size) {
  const struct tm t = FlashcardSrs::daysToTm(day);
  snprintf(out, size, "%02d/%02d", t.tm_mday, t.tm_mon + 1);
}

void range(const uint32_t from, const uint32_t to, char* out, const size_t size) {
  char a[8];
  char b[8];
  dayMonth(from, a, sizeof(a));
  dayMonth(to, b, sizeof(b));
  const struct tm t = FlashcardSrs::daysToTm(to);
  if (from == to) {
    snprintf(out, size, "%s/%04d", a, t.tm_year + 1900);
  } else {
    snprintf(out, size, "%s - %s/%04d", a, b, t.tm_year + 1900);
  }
}

const char* periodName(const int p) {
  switch (p) {
    case 0:
      return tr(STR_FLASHCARD_TODAY);
    case 1:
      return tr(STR_FLASHCARD_WEEK);
    default:
      return tr(STR_FLASHCARD_MONTH);
  }
}

const char* scaleName(const Scale s) {
  switch (s) {
    case Scale::Days:
      return tr(STR_FLASHCARD_SCALE_DAYS);
    case Scale::Weeks:
      return tr(STR_FLASHCARD_SCALE_WEEKS);
    default:
      return tr(STR_FLASHCARD_SCALE_MONTHS);
  }
}

int bucketCount(const Scale s) { return s == Scale::Days ? 30 : 12; }

const char* stateName(const FlashcardDeckState s) {
  switch (s) {
    case FlashcardDeckState::Active:
      return tr(STR_FLASHCARD_ACTIVE);
    case FlashcardDeckState::ReviewsOnly:
      return tr(STR_FLASHCARD_REVIEWS_ONLY);
    default:
      return tr(STR_FLASHCARD_STANDBY);
  }
}

std::string pctText(const int pct) {
  if (pct < 0) return "-";
  return std::to_string(pct) + "%";
}

int legendWidth(const GfxRenderer& renderer, const char* text) {
  const int box = std::max(8, renderer.getLineHeight(SMALL_FONT_ID) / 2);
  return box + 5 + renderer.getTextWidth(SMALL_FONT_ID, text) + 14;
}

void centeredText(const GfxRenderer& renderer, const int font, const int y, const char* text,
                  const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  renderer.drawCenteredText(font, y, text, true, style);
}
}  // namespace

// ─── Data ────────────────────────────────────────────────────────────────────

void FlashcardStatsActivity::onEnter() {
  Activity::onEnter();
  loading = true;
  requestUpdateAndWait();  // "Loading..." while every deck is read
  {
    RenderLock lock(*this);
    load();
    loading = false;
  }
  requestUpdate();
}

void FlashcardStatsActivity::load() {
  FlashcardHistory::flush();
  today = FlashcardSrs::getToday();

  recent.clear();
  summary = FlashcardHistory::Summary{};
  if (today != 0) {
    uint32_t from = std::min(FlashcardPeriods::monthStart(today), FlashcardPeriods::weekStart(today));
    from = std::min(from, today >= 6 ? today - 6 : 0);
    recent = FlashcardHistory::readRange(from, today);
    summary = FlashcardHistory::summarize(today, FLASHCARD_STORE.freeDaysMask);
  }

  decks.clear();
  hard.clear();
  hard.reserve(FlashcardStatsScan::MAX_HARD + 1);
  phases = FlashcardPhases{};
  std::fill(std::begin(forecast), std::end(forecast), 0);
  const auto paths = FlashcardCatalog::allDecks();
  decks.reserve(paths.size());
  for (const auto& path : paths) {
    FlashcardCatalog::Entry e;
    FlashcardCatalog::describe(path, e);
    DeckRow row;
    row.name = e.name;
    row.folder = e.folder;
    uint32_t deckForecast[FlashcardStatsScan::FORECAST_DAYS] = {};
    FlashcardStatsScan::DeckResult r;
    row.loaded = FlashcardStatsScan::scanDeck(path, today, r, deckForecast, &hard);
    row.truncated = r.truncated;
    row.phases = r.phases;
    row.state = FLASHCARD_DECK_OPTIONS.stateFor(path, r.stats);
    if (row.loaded && !row.truncated && row.state != FlashcardDeckState::StandBy) {
      phases.add(r.phases);
      for (int i = 0; i < FlashcardStatsScan::FORECAST_DAYS; ++i) forecast[i] += deckForecast[i];
    }
    if (const auto* lin = FLASHCARD_DECK_OPTIONS.linearResult(path)) {
      row.linearPasses = lin->passes;
      row.lastKnownPct = lin->lastKnownPct;
    }
    decks.push_back(std::move(row));
  }
}

void FlashcardStatsActivity::periodRange(uint32_t& from, uint32_t& to) const {
  to = today;
  switch (period) {
    case Period::Today:
      from = today;
      break;
    case Period::Week:
      from = FlashcardPeriods::weekStart(today);
      break;
    case Period::Month:
      from = FlashcardPeriods::monthStart(today);
      break;
  }
}

void FlashcardStatsActivity::loadHistory() {
  buckets.clear();
  if (today == 0) return;
  buckets = FlashcardPeriods::buckets(scale, today, bucketCount(scale), pagesBack);
  if (buckets.empty()) return;
  FlashcardPeriods::fill(buckets, FlashcardHistory::readRange(buckets.front().start, buckets.back().end));
}

bool FlashcardStatsActivity::canGoOlder() const {
  return !buckets.empty() && summary.firstDay != 0 && buckets.front().start > summary.firstDay;
}

void FlashcardStatsActivity::studyHard() {
  startActivityForResult(std::make_unique<FlashcardReviewActivity>(
                             renderer, mappedInput, FLASHCARD_HARD_KEY, FlashcardReviewActivity::ReviewMode::Linear,
                             FLASHCARD_STORE.shuffle != 0, true, FlashcardNextDeckProvider{}),
                         [this](const ActivityResult&) {
                           // The session added study time and cards to today.
                           RenderLock lock(*this);
                           load();
                         });
}

// ─── Input ───────────────────────────────────────────────────────────────────

void FlashcardStatsActivity::loop() {
  if (loading) return;
  switch (view) {
    case View::Panel:
      loopPanel();
      break;
    case View::History:
      loopHistory();
      break;
    case View::Forecast:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        view = View::Panel;
        requestUpdate();
      }
      break;
    case View::Decks:
      loopList(static_cast<int>(decks.size()));
      break;
    case View::Hard:
      loopList(hard.empty() ? 0 : static_cast<int>(hard.size()) + 1);
      break;
  }
}

void FlashcardStatsActivity::loopPanel() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  // Front Left/Right: period. Side Up/Down: menu.
  if (mappedInput.wasReleased(Button::Left) && period != Period::Today) {
    period = static_cast<Period>(static_cast<int>(period) - 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Right) && period != Period::Month) {
    period = static_cast<Period>(static_cast<int>(period) + 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Up)) {
    menuIndex = ButtonNavigator::previousIndex(menuIndex, MENU_COUNT);
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Down)) {
    menuIndex = ButtonNavigator::nextIndex(menuIndex, MENU_COUNT);
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    RenderLock lock(*this);
    listIndex = 0;
    switch (menuIndex) {
      case MENU_HISTORY:
        scale = period == Period::Today ? Scale::Days : period == Period::Week ? Scale::Weeks : Scale::Months;
        pagesBack = 0;
        loadHistory();
        view = View::History;
        break;
      case MENU_FORECAST:
        view = View::Forecast;
        break;
      case MENU_DECKS:
        view = View::Decks;
        break;
      default:
        view = View::Hard;
        break;
    }
    lock.unlock();
    requestUpdate();
  }
}

void FlashcardStatsActivity::loopHistory() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    view = View::Panel;
    requestUpdate();
    return;
  }
  bool changed = false;
  RenderLock lock(*this);
  if (mappedInput.wasReleased(Button::Confirm)) {
    scale = scale == Scale::Days ? Scale::Weeks : scale == Scale::Weeks ? Scale::Months : Scale::Days;
    pagesBack = 0;
    changed = true;
  } else if ((mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Up)) && canGoOlder()) {
    ++pagesBack;
    changed = true;
  } else if ((mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::Down)) && pagesBack > 0) {
    --pagesBack;
    changed = true;
  }
  if (changed) loadHistory();
  lock.unlock();
  if (changed) requestUpdate();
}

void FlashcardStatsActivity::loopList(const int count) {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    view = View::Panel;
    requestUpdate();
    return;
  }
  if (count > 0) {
    buttonNavigator.onNext([this, count] {
      listIndex = ButtonNavigator::nextIndex(listIndex, count);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, count] {
      listIndex = ButtonNavigator::previousIndex(listIndex, count);
      requestUpdate();
    });
  }
  if (view == View::Hard && count > 0 && listIndex == 0 &&
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    studyHard();
  }
}

// ─── Render ──────────────────────────────────────────────────────────────────

void FlashcardStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  const char* title = tr(STR_FLASHCARD_STATS);
  switch (view) {
    case View::History:
      title = tr(STR_FLASHCARD_HISTORY);
      break;
    case View::Forecast:
      title = tr(STR_FLASHCARD_FORECAST);
      break;
    case View::Decks:
      title = tr(STR_FLASHCARD_DECK_LIST);
      break;
    case View::Hard:
      title = tr(STR_FLASHCARD_HARD_CARDS);
      break;
    case View::Panel:
      break;
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (loading) {
    centeredText(renderer, UI_12_FONT_ID, top + (bottom - top) / 3, tr(STR_LOADING), EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  const char* b3 = tr(STR_DIR_UP);
  const char* b4 = tr(STR_DIR_DOWN);
  const char* b2 = "";
  switch (view) {
    case View::Panel:
      renderPanel(top, bottom);
      b2 = tr(STR_SELECT);
      b3 = period == Period::Today ? "" : periodName(static_cast<int>(period) - 1);
      b4 = period == Period::Month ? "" : periodName(static_cast<int>(period) + 1);
      break;
    case View::History:
      renderHistory(top, bottom);
      b2 = tr(STR_FLASHCARD_SCALE);
      b3 = canGoOlder() ? tr(STR_FLASHCARD_OLDER) : "";
      b4 = pagesBack > 0 ? tr(STR_FLASHCARD_NEWER) : "";
      break;
    case View::Forecast:
      renderForecast(top, bottom);
      b3 = "";
      b4 = "";
      break;
    case View::Decks:
      renderDecks(top, bottom);
      break;
    case View::Hard:
      renderHard(top, bottom);
      if (!hard.empty() && listIndex == 0) b2 = tr(STR_SELECT);
      break;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), b2, b3, b4);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void FlashcardStatsActivity::renderPanel(const int top, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int side = metrics.contentSidePadding + 6;
  const int innerW = pageWidth - side * 2;
  const int uiH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  int y = top;

  // Period selector: ◀ Week ▶
  const char* name = periodName(static_cast<int>(period));
  const int nameW = renderer.getTextWidth(UI_12_FONT_ID, name, EpdFontFamily::BOLD);
  centeredText(renderer, UI_12_FONT_ID, y, name, EpdFontFamily::BOLD);
  const int arrowY = y + uiH / 2;
  if (period != Period::Today) FlashcardChart::drawArrow(renderer, (pageWidth - nameW) / 2 - 22, arrowY, 14, true);
  if (period != Period::Month) FlashcardChart::drawArrow(renderer, (pageWidth + nameW) / 2 + 22, arrowY, 14, false);
  y += uiH;

  // Menu rows at the bottom; the chart takes what is left above them.
  const int rowH = 20 + uiH;
  const int menuTop = bottom - rowH * MENU_COUNT;

  if (today == 0) {
    centeredText(renderer, UI_10_FONT_ID, y + smallH, tr(STR_FLASHCARD_CLOCK_NOT_SET));
  } else {
    uint32_t from = 0;
    uint32_t to = 0;
    periodRange(from, to);
    char buf[64];
    range(from, to, buf, sizeof(buf));
    centeredText(renderer, SMALL_FONT_ID, y, buf);
    y += smallH + 6;

    const FlashcardDayRecord s = FlashcardPeriods::sum(recent, from, to);

    // Big number with the study time: "310 cards · 2h05"
    const int bigFont = NOTOSERIF_24_FONT_ID;
    const int bigH = renderer.getLineHeight(bigFont);
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s.cards()));
    const std::string unit =
        std::string(tr(STR_FLASHCARD_CARDS)) + " \xC2\xB7 " + FlashcardChart::formatDuration(s.seconds());
    const int numW = renderer.getTextWidth(bigFont, buf, EpdFontFamily::BOLD);
    const int unitW = renderer.getTextWidth(UI_12_FONT_ID, unit.c_str());
    const int x0 = (pageWidth - numW - 10 - unitW) / 2;
    renderer.drawText(bigFont, x0, y, buf, true, EpdFontFamily::BOLD);
    // Same baseline as the number
    const int unitY = y + renderer.getFontAscenderSize(bigFont) - renderer.getFontAscenderSize(UI_12_FONT_ID);
    renderer.drawText(UI_12_FONT_ID, x0 + numW + 10, unitY, unit.c_str());
    y += bigH;

    // Spaced / Linear split
    const FlashcardChart::Segment split[2] = {{s.spaced(), FlashcardChart::Fill::Black},
                                              {s.linearCards, FlashcardChart::Fill::Dots}};
    FlashcardChart::drawStacked(renderer, side, y, innerW, 16, split, 2);
    y += 16 + 6;
    const std::string l1 = std::string(tr(STR_FLASHCARD_SPACED_SHORT)) + " " + std::to_string(s.spaced());
    const std::string l2 = std::string(tr(STR_FLASHCARD_LINEAR_SHORT)) + " " + std::to_string(s.linearCards);
    const int legendW = legendWidth(renderer, l1.c_str()) + legendWidth(renderer, l2.c_str()) - 14;
    int lx = (pageWidth - legendW) / 2;
    lx = FlashcardChart::drawLegend(renderer, lx, y, FlashcardChart::Fill::Black, l1.c_str());
    FlashcardChart::drawLegend(renderer, lx, y, FlashcardChart::Fill::Dots, l2.c_str());
    y += smallH + 10;

    // Tiles: streak, new cards and retention (Spaced)
    const int tileH = FlashcardChart::tileHeight(renderer, true);
    const int tileW = (innerW - GAP * 2) / 3;
    char value[16];
    char sub[32];
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(summary.currentStreak));
    snprintf(sub, sizeof(sub), "%s %u", tr(STR_FLASHCARD_RECORD), static_cast<unsigned>(summary.longestStreak));
    FlashcardChart::drawTile(renderer, Rect{side, y, tileW, tileH}, value, tr(STR_FLASHCARD_STREAK), sub);
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(s.newCards));
    FlashcardChart::drawTile(renderer, Rect{side + tileW + GAP, y, tileW, tileH}, value, tr(STR_FLASHCARD_NEW_LC),
                             tr(STR_FLASHCARD_SPACED_SHORT));
    const std::string ret = pctText(FlashcardPeriods::percent(s.reviewCorrect, s.reviewCards));
    FlashcardChart::drawTile(renderer, Rect{side + (tileW + GAP) * 2, y, tileW, tileH}, ret.c_str(),
                             tr(STR_FLASHCARD_RETENTION), tr(STR_FLASHCARD_SPACED_SHORT));
    y += tileH + 10;

    // Mini chart: last 7 days (Today), this week by day, this month by week
    const int chartH = menuTop - GAP - y;
    if (chartH >= 70) {
      FlashcardChart::Bar bars[7];
      char labels[7][8];
      int count = 0;
      if (period == Period::Month) {
        const uint32_t mStart = FlashcardPeriods::monthStart(today);
        for (uint32_t ws = FlashcardPeriods::weekStart(mStart); ws <= today && count < 7; ws += 7) {
          const uint32_t a = std::max(ws, mStart);
          const uint32_t b = std::min(ws + 6, today);
          const FlashcardDayRecord w = FlashcardPeriods::sum(recent, a, b);
          bars[count].base = w.spaced();
          bars[count].top = w.linearCards;
          dayMonth(a, labels[count], sizeof(labels[count]));
          bars[count].label = labels[count];
          bars[count].mark = today >= ws && today <= ws + 6;
          ++count;
        }
      } else {
        const uint32_t first = period == Period::Week ? FlashcardPeriods::weekStart(today) : today - 6;
        for (int i = 0; i < 7; ++i) {
          const uint32_t d = first + static_cast<uint32_t>(i);
          const FlashcardDayRecord r = d <= today ? FlashcardPeriods::sum(recent, d, d) : FlashcardDayRecord{};
          bars[i].base = r.spaced();
          bars[i].top = r.linearCards;
          weekdayInitial(d, labels[i], sizeof(labels[i]));
          bars[i].label = labels[i];
          bars[i].mark = d == today;
        }
        count = 7;
      }
      FlashcardChart::drawBars(renderer, Rect{side, y, innerW, chartH}, bars, count, true);
    }
  }

  // Menu
  FlashcardUi::Row rows[MENU_COUNT];
  std::string values[MENU_COUNT];
  rows[MENU_HISTORY].title = tr(STR_FLASHCARD_HISTORY);
  rows[MENU_FORECAST].title = tr(STR_FLASHCARD_FORECAST);
  if (today != 0) {
    values[MENU_FORECAST] = std::string(tr(STR_FLASHCARD_TOMORROW)) + " " + std::to_string(forecast[1]);
    rows[MENU_FORECAST].value = values[MENU_FORECAST].c_str();
  }
  rows[MENU_DECKS].title = tr(STR_FLASHCARD_DECK_LIST);
  values[MENU_DECKS] = std::to_string(decks.size());
  rows[MENU_DECKS].value = values[MENU_DECKS].c_str();
  rows[MENU_HARD].title = tr(STR_FLASHCARD_HARD_CARDS);
  values[MENU_HARD] = std::to_string(hard.size());
  rows[MENU_HARD].value = values[MENU_HARD].c_str();
  renderer.drawLine(side, menuTop - 3, pageWidth - side, menuTop - 3, true);
  FlashcardUi::drawRows(renderer, Rect{0, menuTop, pageWidth, bottom - menuTop}, rows, MENU_COUNT, menuIndex);
}

void FlashcardStatsActivity::renderHistory(const int top, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int side = metrics.contentSidePadding + 6;
  const int innerW = pageWidth - side * 2;
  const int uiH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  int y = top;

  centeredText(renderer, UI_12_FONT_ID, y, scaleName(scale), EpdFontFamily::BOLD);
  y += uiH;
  if (buckets.empty()) {
    centeredText(renderer, UI_10_FONT_ID, y + smallH, tr(STR_FLASHCARD_CLOCK_NOT_SET));
    return;
  }
  char buf[96];
  const uint32_t lastDay = std::min(buckets.back().end, today);
  range(buckets.front().start, lastDay, buf, sizeof(buf));
  centeredText(renderer, SMALL_FONT_ID, y, buf);
  y += smallH + 6;

  FlashcardDayRecord total;
  const FlashcardPeriods::Bucket* best = nullptr;
  for (const auto& b : buckets) {
    total.add(b.sum);
    if (!best || b.sum.cards() > best->sum.cards()) best = &b;
  }

  // Chart
  const int n = static_cast<int>(buckets.size());
  std::vector<FlashcardChart::Bar> bars(n);
  std::vector<std::array<char, 8>> labels(n);
  for (int i = 0; i < n; ++i) {
    const auto& b = buckets[i];
    bars[i].base = b.sum.spaced();
    bars[i].top = b.sum.linearCards;
    bars[i].mark = today >= b.start && today <= b.end;
    const int fromEnd = n - 1 - i;
    bool labelled = false;
    const struct tm t = FlashcardSrs::daysToTm(b.start);
    switch (scale) {
      case Scale::Days:
        labelled = fromEnd % 5 == 0;
        snprintf(labels[i].data(), labels[i].size(), "%d", t.tm_mday);
        break;
      case Scale::Weeks:
        labelled = fromEnd % 3 == 0;
        dayMonth(b.start, labels[i].data(), labels[i].size());
        break;
      case Scale::Months:
        labelled = true;
        snprintf(labels[i].data(), labels[i].size(), "%02d", t.tm_mon + 1);
        break;
    }
    bars[i].label = labelled ? labels[i].data() : nullptr;
  }
  const int chartH = std::max(120, (bottom - y) * 42 / 100);
  FlashcardChart::drawBars(renderer, Rect{side, y, innerW, chartH}, bars.data(), n, scale != Scale::Days);
  if (total.cards() == 0) centeredText(renderer, UI_10_FONT_ID, y + chartH / 3, tr(STR_FLASHCARD_NO_STUDY));
  y += chartH + 4;

  const std::string l1 = tr(STR_FLASHCARD_SPACED_SHORT);
  const std::string l2 = tr(STR_FLASHCARD_LINEAR_SHORT);
  const int legendW = legendWidth(renderer, l1.c_str()) + legendWidth(renderer, l2.c_str()) - 14;
  int lx = (pageWidth - legendW) / 2;
  lx = FlashcardChart::drawLegend(renderer, lx, y, FlashcardChart::Fill::Black, l1.c_str());
  FlashcardChart::drawLegend(renderer, lx, y, FlashcardChart::Fill::Dots, l2.c_str());
  y += smallH + 10;

  // Tiles: cards, time, cards per day
  const uint32_t days = lastDay >= buckets.front().start ? lastDay - buckets.front().start + 1 : 1;
  const int tileH = FlashcardChart::tileHeight(renderer, false);
  const int tileW = (innerW - GAP * 2) / 3;
  char value[16];
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(total.cards()));
  FlashcardChart::drawTile(renderer, Rect{side, y, tileW, tileH}, value, tr(STR_FLASHCARD_CARDS));
  const std::string dur = FlashcardChart::formatDuration(total.seconds());
  FlashcardChart::drawTile(renderer, Rect{side + tileW + GAP, y, tileW, tileH}, dur.c_str(), tr(STR_FLASHCARD_TIME));
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>((total.cards() + days / 2) / days));
  FlashcardChart::drawTile(renderer, Rect{side + (tileW + GAP) * 2, y, tileW, tileH}, value, tr(STR_FLASHCARD_PER_DAY));
  y += tileH + 10;

  // Spaced ratings: one black bar per button
  if (y + smallH * 6 > bottom) return;
  const char* ratingNames[4] = {tr(STR_FLASHCARD_AGAIN), tr(STR_FLASHCARD_HARD), tr(STR_FLASHCARD_GOOD),
                                tr(STR_FLASHCARD_EASY)};
  const uint32_t ratingValues[4] = {total.again, total.hard, total.good, total.easy};
  y += FlashcardChart::drawHBars(renderer, side, y, innerW, ratingNames, ratingValues, 4) + 6;
  const std::string ret = pctText(FlashcardPeriods::percent(total.reviewCorrect, total.reviewCards));
  snprintf(buf, sizeof(buf), "%s %u \xC2\xB7 %s %u \xC2\xB7 %s %s", tr(STR_FLASHCARD_SPACED_SHORT),
           static_cast<unsigned>(total.spaced()), tr(STR_FLASHCARD_NEW_LC), static_cast<unsigned>(total.newCards),
           tr(STR_FLASHCARD_RETENTION), ret.c_str());
  centeredText(renderer, SMALL_FONT_ID, y, buf);
  y += smallH + 2;
  if (best && best->sum.cards() > 0) {
    char when[8];
    dayMonth(best->start, when, sizeof(when));
    snprintf(buf, sizeof(buf), "%s: %lu (%s)", tr(STR_FLASHCARD_BEST_DAY),
             static_cast<unsigned long>(best->sum.cards()), when);
    if (scale == Scale::Days) centeredText(renderer, SMALL_FONT_ID, y, buf);
  }
}

void FlashcardStatsActivity::renderForecast(const int top, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int side = metrics.contentSidePadding + 6;
  const int innerW = pageWidth - side * 2;
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  int y = top;
  if (today == 0) {
    centeredText(renderer, UI_10_FONT_ID, y + smallH, tr(STR_FLASHCARD_CLOCK_NOT_SET));
    return;
  }

  uint32_t week = 0;
  uint32_t month = 0;
  for (int i = 0; i < FlashcardStatsScan::FORECAST_DAYS; ++i) {
    if (i >= 1 && i <= 7) week += forecast[i];
    month += forecast[i];
  }
  const int tileH = FlashcardChart::tileHeight(renderer, false);
  const int tileW = (innerW - GAP) / 2;
  const std::string v[4] = {std::to_string(forecast[0]), std::to_string(forecast[1]), std::to_string(week),
                            std::to_string(month)};
  const char* l[4] = {tr(STR_FLASHCARD_TODAY), tr(STR_FLASHCARD_TOMORROW), tr(STR_FLASHCARD_NEXT_7),
                      tr(STR_FLASHCARD_NEXT_30)};
  for (int i = 0; i < 4; ++i) {
    const int tx = side + (i % 2) * (tileW + GAP);
    const int ty = y + (i / 2) * (tileH + GAP);
    FlashcardChart::drawTile(renderer, Rect{tx, ty, tileW, tileH}, v[i].c_str(), l[i]);
  }
  y += tileH * 2 + GAP + 12;

  FlashcardChart::Bar bars[FlashcardStatsScan::FORECAST_DAYS];
  char labels[FlashcardStatsScan::FORECAST_DAYS][8];
  for (int i = 0; i < FlashcardStatsScan::FORECAST_DAYS; ++i) {
    bars[i].base = forecast[i];
    bars[i].mark = i == 0;
    const struct tm t = FlashcardSrs::daysToTm(today + static_cast<uint32_t>(i));
    snprintf(labels[i], sizeof(labels[i]), "%d", t.tm_mday);
    bars[i].label = i % 5 == 0 ? labels[i] : nullptr;
  }
  FlashcardChart::drawBars(renderer, Rect{side, y, innerW, bottom - y}, bars, FlashcardStatsScan::FORECAST_DAYS, false);
}

void FlashcardStatsActivity::renderDecks(const int top, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int side = metrics.contentSidePadding + 6;
  const int innerW = pageWidth - side * 2;
  const int uiH = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  if (decks.empty()) {
    centeredText(renderer, UI_12_FONT_ID, top + 10, tr(STR_FLASHCARD_NO_DECKS), EpdFontFamily::BOLD);
    return;
  }

  // Legend
  int lx = side;
  lx = FlashcardChart::drawLegend(renderer, lx, top, FlashcardChart::Fill::Black, tr(STR_FLASHCARD_MATURE));
  lx = FlashcardChart::drawLegend(renderer, lx, top, FlashcardChart::Fill::Dots, tr(STR_FLASHCARD_LEARNING));
  FlashcardChart::drawLegend(renderer, lx, top, FlashcardChart::Fill::White, tr(STR_FLASHCARD_NEW_LC));
  const int listTop = top + smallH + 8;

  constexpr int barH = 12;
  const int itemH = uiH + 4 + barH + 4 + smallH * 2 + 12;
  const int visible = std::max(1, (bottom - listTop) / itemH);
  const int count = static_cast<int>(decks.size());
  const int first = std::clamp(listIndex - visible + 1, 0, std::max(0, count - visible));
  int y = listTop;
  char buf[128];
  for (int i = first; i < count && i < first + visible; ++i) {
    const DeckRow& d = decks[i];
    const int x = side + 10;
    const int w = innerW - 10;
    if (i == listIndex) renderer.fillRect(side, y + 2, 5, itemH - 8, true);

    const char* state = !d.loaded     ? tr(STR_FLASHCARD_NOT_LOADED)
                        : d.truncated ? tr(STR_FLASHCARD_LINEAR_ONLY)
                                      : stateName(d.state);
    const int stateW = renderer.getTextWidth(UI_10_FONT_ID, state, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + w - stateW, y + 2, state, true, EpdFontFamily::BOLD);
    const std::string name = renderer.truncatedText(UI_12_FONT_ID, d.name.c_str(), w - stateW - 12);
    renderer.drawText(UI_12_FONT_ID, x, y, name.c_str(), true,
                      i == listIndex ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    int ly = y + uiH + 4;
    const FlashcardChart::Segment segs[3] = {{d.phases.mature, FlashcardChart::Fill::Black},
                                             {d.phases.learning, FlashcardChart::Fill::Dots},
                                             {d.phases.newCards, FlashcardChart::Fill::White}};
    FlashcardChart::drawStacked(renderer, x, ly, w, barH, segs, 3);
    ly += barH + 4;
    snprintf(buf, sizeof(buf), "%s%s%lu %s \xC2\xB7 %lu %s \xC2\xB7 %lu %s", d.folder.c_str(),
             d.folder.empty() ? "" : " \xC2\xB7 ", static_cast<unsigned long>(d.phases.mature),
             tr(STR_FLASHCARD_MATURE), static_cast<unsigned long>(d.phases.learning), tr(STR_FLASHCARD_LEARNING),
             static_cast<unsigned long>(d.phases.newCards), tr(STR_FLASHCARD_NEW_LC));
    std::string line = renderer.truncatedText(SMALL_FONT_ID, buf, w);
    renderer.drawText(SMALL_FONT_ID, x, ly, line.c_str());
    ly += smallH;
    if (d.linearPasses > 0) {
      snprintf(buf, sizeof(buf), "%s: %s %d%% %s \xC2\xB7 %s: %u", tr(STR_FLASHCARD_LINEAR_SHORT),
               tr(STR_FLASHCARD_LAST_PASS), d.lastKnownPct, tr(STR_FLASHCARD_KNOWN_PCT), tr(STR_FLASHCARD_PASSES),
               static_cast<unsigned>(d.linearPasses));
      line = renderer.truncatedText(SMALL_FONT_ID, buf, w);
      renderer.drawText(SMALL_FONT_ID, x, ly, line.c_str());
    }
    y += itemH;
  }
}

void FlashcardStatsActivity::renderHard(const int top, const int bottom) {
  const int pageWidth = renderer.getScreenWidth();
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  if (hard.empty()) {
    centeredText(renderer, UI_12_FONT_ID, top + 20, tr(STR_FLASHCARD_NO_HARD), EpdFontFamily::BOLD);
    centeredText(renderer, SMALL_FONT_ID, top + 20 + renderer.getLineHeight(UI_12_FONT_ID) + 6,
                 tr(STR_FLASHCARD_HARD_HINT));
    return;
  }
  centeredText(renderer, SMALL_FONT_ID, top, tr(STR_FLASHCARD_HARD_HINT));
  const int listTop = top + smallH + 6;

  const size_t n = hard.size() + 1;
  std::vector<FlashcardUi::Row> rows(n);
  std::vector<std::string> subs(n);
  rows[0].title = tr(STR_FLASHCARD_STUDY_HARD);
  subs[0] = std::to_string(hard.size()) + " " + tr(STR_FLASHCARD_CARDS);
  rows[0].subtitle = subs[0].c_str();
  for (size_t i = 0; i < hard.size(); ++i) {
    const auto& c = hard[i];
    rows[i + 1].title = c.front.c_str();
    // The selected card shows its answer.
    if (static_cast<int>(i + 1) == listIndex) {
      subs[i + 1] = c.back;
    } else {
      subs[i + 1] = c.deckName + " \xC2\xB7 " + tr(STR_FLASHCARD_EASE) + " " + std::to_string(c.ease) + "%";
    }
    rows[i + 1].subtitle = subs[i + 1].c_str();
  }
  FlashcardUi::drawRows(renderer, Rect{0, listTop, pageWidth, bottom - listTop}, rows.data(), static_cast<int>(n),
                        listIndex);
}
