#include "FlashcardDeckOptions.h"

#include <algorithm>

namespace {
constexpr uint8_t FORMAT_VERSION = 1;

FlashcardDeckState stateFromInt(const uint8_t v) {
  switch (v) {
    case 0:
      return FlashcardDeckState::Active;
    case 1:
      return FlashcardDeckState::ReviewsOnly;
    default:
      return FlashcardDeckState::StandBy;
  }
}

uint8_t clampShare(const int v) {
  return static_cast<uint8_t>(
      std::clamp(v, static_cast<int>(FlashcardPlanner::SHARE_MIN), static_cast<int>(FlashcardPlanner::SHARE_MAX)));
}
}  // namespace

void FlashcardDeckOptions::toJson(JsonDocument& doc) const {
  doc["v"] = FORMAT_VERSION;
  JsonArray decks = doc["decks"].to<JsonArray>();
  for (const auto& o : options) {
    JsonObject d = decks.add<JsonObject>();
    d["p"] = o.path.c_str();
    d["s"] = static_cast<uint8_t>(o.state);
    d["n"] = o.share;
  }
  doc["splitDay"] = splitDay;
  doc["splitValid"] = splitValid;
  JsonArray split = doc["split"].to<JsonArray>();
  for (const auto& a : allots) {
    JsonObject s = split.add<JsonObject>();
    s["p"] = a.path.c_str();
    s["a"] = a.allot;
    s["u"] = a.used;
  }
  JsonArray lin = doc["linear"].to<JsonArray>();
  for (const auto& r : linear) {
    JsonObject l = lin.add<JsonObject>();
    l["p"] = r.path.c_str();
    l["c"] = r.passes;
    l["k"] = r.lastKnownPct;
  }
}

bool FlashcardDeckOptions::fromJson(JsonVariantConst doc) {
  options.clear();
  allots.clear();
  linear.clear();
  splitDay = 0;
  splitValid = false;
  if ((doc["v"] | static_cast<uint8_t>(0)) != FORMAT_VERSION) return false;

  JsonArrayConst decks = doc["decks"].as<JsonArrayConst>();
  options.reserve(decks.size());
  for (JsonVariantConst d : decks) {
    const char* path = d["p"] | "";
    if (path[0] == '\0' || find(path)) continue;
    Option o;
    o.path = path;
    o.state = stateFromInt(d["s"] | static_cast<uint8_t>(0));
    o.share = clampShare(d["n"] | static_cast<int>(FlashcardPlanner::DEFAULT_SHARE));
    options.push_back(std::move(o));
  }

  splitDay = doc["splitDay"] | static_cast<uint32_t>(0);
  splitValid = doc["splitValid"] | false;
  JsonArrayConst split = doc["split"].as<JsonArrayConst>();
  allots.reserve(split.size());
  for (JsonVariantConst s : split) {
    FlashcardPlanner::Allot a;
    a.path = s["p"] | "";
    a.allot = s["a"] | static_cast<uint16_t>(0);
    a.used = s["u"] | static_cast<uint16_t>(0);
    if (a.path.empty()) continue;
    a.allot = std::max(a.allot, a.used);
    allots.push_back(std::move(a));
  }

  JsonArrayConst lin = doc["linear"].as<JsonArrayConst>();
  linear.reserve(lin.size());
  for (JsonVariantConst l : lin) {
    const char* path = l["p"] | "";
    if (path[0] == '\0' || linearResult(path)) continue;
    LinearResult r;
    r.path = path;
    r.passes = l["c"] | static_cast<uint16_t>(0);
    r.lastKnownPct = std::min<uint8_t>(l["k"] | static_cast<uint8_t>(0), 100);
    linear.push_back(std::move(r));
  }
  return true;
}

const FlashcardDeckOptions::Option* FlashcardDeckOptions::find(const std::string& path) const {
  for (const auto& o : options) {
    if (o.path == path) return &o;
  }
  return nullptr;
}

FlashcardDeckState FlashcardDeckOptions::stateFor(const std::string& path, const DeckStats& stats) const {
  const Option* o = find(path);
  return o ? o->state : FlashcardPlanner::defaultState(stats);
}

uint8_t FlashcardDeckOptions::shareFor(const std::string& path) const {
  const Option* o = find(path);
  return o ? o->share : FlashcardPlanner::DEFAULT_SHARE;
}

void FlashcardDeckOptions::set(const std::string& path, const FlashcardDeckState state, const uint8_t share) {
  const uint8_t s = clampShare(share);
  for (auto& o : options) {
    if (o.path == path) {
      if (o.state == state && o.share == s) return;
      o.state = state;
      o.share = s;
      splitValid = false;
      return;
    }
  }
  Option o;
  o.path = path;
  o.state = state;
  o.share = s;
  options.push_back(std::move(o));
  splitValid = false;
}

bool FlashcardDeckOptions::refreshSplit(const std::vector<FlashcardPlanner::PlanDeck>& decks, const uint32_t today,
                                        const uint16_t total, const uint16_t usedToday, const uint32_t seed) {
  if (today == 0) return false;
  if (splitDay == today && splitValid) return false;
  // Same day: keep what each deck already used. New day: start over.
  const std::vector<FlashcardPlanner::Allot> previous =
      splitDay == today ? allots : std::vector<FlashcardPlanner::Allot>{};
  allots = FlashcardPlanner::split(decks, previous, total, usedToday, seed);
  splitDay = today;
  splitValid = true;
  return true;
}

uint16_t FlashcardDeckOptions::newRemaining(const std::string& path, const uint32_t today) const {
  if (today == 0 || splitDay != today) return 0;
  return FlashcardPlanner::remaining(allots, path);
}

void FlashcardDeckOptions::recordNew(const std::string& path, const uint32_t today) {
  if (today == 0 || splitDay != today) return;
  for (auto& a : allots) {
    if (a.path == path) {
      ++a.used;
      a.allot = std::max(a.allot, a.used);
      return;
    }
  }
}

bool FlashcardDeckOptions::prune(const std::vector<std::string>& existingPaths) {
  const auto gone = [&existingPaths](const std::string& path) {
    return std::find(existingPaths.begin(), existingPaths.end(), path) == existingPaths.end();
  };
  const size_t before = options.size() + linear.size();
  options.erase(std::remove_if(options.begin(), options.end(), [&gone](const Option& o) { return gone(o.path); }),
                options.end());
  linear.erase(std::remove_if(linear.begin(), linear.end(), [&gone](const LinearResult& r) { return gone(r.path); }),
               linear.end());
  return options.size() + linear.size() != before;
}

void FlashcardDeckOptions::recordLinearPass(const std::string& path, const size_t known, const size_t total) {
  if (total == 0) return;
  const uint8_t pct = static_cast<uint8_t>(std::min<size_t>((known * 100 + total / 2) / total, 100));
  for (auto& r : linear) {
    if (r.path == path) {
      if (r.passes < 0xFFFF) ++r.passes;
      r.lastKnownPct = pct;
      return;
    }
  }
  LinearResult r;
  r.path = path;
  r.passes = 1;
  r.lastKnownPct = pct;
  linear.push_back(std::move(r));
}

const FlashcardDeckOptions::LinearResult* FlashcardDeckOptions::linearResult(const std::string& path) const {
  for (const auto& r : linear) {
    if (r.path == path) return &r;
  }
  return nullptr;
}
