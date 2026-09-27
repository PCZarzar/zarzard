#include "FlashcardHistory.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

#include "FlashcardClock.h"

namespace {
constexpr const char* TAG = "FCHIST";
constexpr const char* TMP_PATH = "/.crosspoint/flashcard_history.bin.tmp";
constexpr uint32_t FORMAT_SEED = 0x46434831u;  // "FCH1": a new record layout gets a new seed
constexpr size_t FIELD_COUNT = 13;             // day + 12 counters, uint16 each

uint16_t sat16(const uint32_t v) { return static_cast<uint16_t>(std::min<uint32_t>(v, 0xFFFF)); }

uint16_t checksum(const uint8_t* data, const size_t len) {
  uint32_t h = 2166136261u ^ FORMAT_SEED;
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619u;
  }
  return static_cast<uint16_t>(h ^ (h >> 16));
}

void fields(FlashcardDayRecord& r, uint16_t* (&out)[FIELD_COUNT - 1]) {
  uint16_t* list[FIELD_COUNT - 1] = {&r.again,         &r.hard,        &r.good,          &r.easy,
                                     &r.newCards,      &r.reviewCards, &r.reviewCorrect, &r.matureReviews,
                                     &r.matureCorrect, &r.linearCards, &r.spacedSeconds, &r.linearSeconds};
  std::copy(std::begin(list), std::end(list), std::begin(out));
}

FlashcardDayRecord current;  // today's record, valid when `loaded`
bool loaded = false;
bool dirty = false;

// An interrupted flush can leave only the .tmp copy.
void recover() {
  if (!Storage.exists(FlashcardHistory::PATH) && Storage.exists(TMP_PATH)) {
    LOG_INF(TAG, "Restoring history from %s", TMP_PATH);
    Storage.rename(TMP_PATH, FlashcardHistory::PATH);
  }
}

// Last complete record of the file; false if none or damaged.
bool readLast(HalFile& file, size_t& whole, FlashcardDayRecord& out) {
  const size_t size = file.size();
  whole = size - size % FlashcardHistory::RECORD_BYTES;
  if (whole < FlashcardHistory::RECORD_BYTES) return false;
  uint8_t buf[FlashcardHistory::RECORD_BYTES];
  if (!file.seek(whole - FlashcardHistory::RECORD_BYTES)) return false;
  if (file.read(buf, sizeof(buf)) != static_cast<int>(sizeof(buf))) return false;
  return FlashcardHistory::decode(buf, out);
}

void beginDay(const uint32_t today) {
  if (loaded && current.day == today) return;
  if (loaded && dirty && !FlashcardHistory::flush()) LOG_ERR(TAG, "Day %lu not saved", (unsigned long)current.day);
  current = FlashcardDayRecord{};
  current.day = today;
  loaded = true;
  dirty = false;
  recover();
  HalFile file;
  if (!Storage.exists(FlashcardHistory::PATH) || !Storage.openFileForRead(TAG, FlashcardHistory::PATH, file)) return;
  size_t whole = 0;
  FlashcardDayRecord last;
  if (readLast(file, whole, last) && last.day == today) current = last;  // continue today's counts
}

// Calls fn(record) for each valid record of the file, in file order.
template <typename Fn>
void forEachRecord(Fn&& fn) {
  recover();
  HalFile file;
  if (!Storage.exists(FlashcardHistory::PATH) || !Storage.openFileForRead(TAG, FlashcardHistory::PATH, file)) return;
  uint8_t buf[FlashcardHistory::RECORD_BYTES * 8];
  size_t have = 0;
  while (true) {
    const int n = file.read(buf + have, sizeof(buf) - have);
    if (n <= 0) break;
    have += static_cast<size_t>(n);
    size_t pos = 0;
    for (; pos + FlashcardHistory::RECORD_BYTES <= have; pos += FlashcardHistory::RECORD_BYTES) {
      FlashcardDayRecord r;
      if (FlashcardHistory::decode(buf + pos, r)) fn(r);
    }
    std::copy(buf + pos, buf + have, buf);
    have -= pos;
  }
}

bool allFree(const uint32_t from, const uint32_t to, const uint8_t mask) {
  // Days strictly between from and to. A week always has a study day, so a
  // longer gap cannot be all free days.
  if (to <= from + 1) return true;
  if (to - from > 7) return false;
  for (uint32_t d = from + 1; d < to; ++d) {
    if (!FlashcardClock::isFreeDay(d, mask)) return false;
  }
  return true;
}
}  // namespace

void FlashcardDayRecord::add(const FlashcardDayRecord& o) {
  again = sat16(static_cast<uint32_t>(again) + o.again);
  hard = sat16(static_cast<uint32_t>(hard) + o.hard);
  good = sat16(static_cast<uint32_t>(good) + o.good);
  easy = sat16(static_cast<uint32_t>(easy) + o.easy);
  newCards = sat16(static_cast<uint32_t>(newCards) + o.newCards);
  reviewCards = sat16(static_cast<uint32_t>(reviewCards) + o.reviewCards);
  reviewCorrect = sat16(static_cast<uint32_t>(reviewCorrect) + o.reviewCorrect);
  matureReviews = sat16(static_cast<uint32_t>(matureReviews) + o.matureReviews);
  matureCorrect = sat16(static_cast<uint32_t>(matureCorrect) + o.matureCorrect);
  linearCards = sat16(static_cast<uint32_t>(linearCards) + o.linearCards);
  spacedSeconds = sat16(static_cast<uint32_t>(spacedSeconds) + o.spacedSeconds);
  linearSeconds = sat16(static_cast<uint32_t>(linearSeconds) + o.linearSeconds);
}

namespace FlashcardHistory {

const char* const PATH = "/.crosspoint/flashcard_history.bin";

void encode(const FlashcardDayRecord& r, uint8_t* out) {
  FlashcardDayRecord copy = r;
  uint16_t* f[FIELD_COUNT - 1];
  fields(copy, f);
  const uint16_t day = sat16(r.day);
  out[0] = static_cast<uint8_t>(day & 0xFF);
  out[1] = static_cast<uint8_t>(day >> 8);
  for (size_t i = 0; i < FIELD_COUNT - 1; ++i) {
    out[2 + i * 2] = static_cast<uint8_t>(*f[i] & 0xFF);
    out[3 + i * 2] = static_cast<uint8_t>(*f[i] >> 8);
  }
  const uint16_t sum = checksum(out, FIELD_COUNT * 2);
  out[FIELD_COUNT * 2] = static_cast<uint8_t>(sum & 0xFF);
  out[FIELD_COUNT * 2 + 1] = static_cast<uint8_t>(sum >> 8);
}

bool decode(const uint8_t* in, FlashcardDayRecord& out) {
  const uint16_t sum = static_cast<uint16_t>(in[FIELD_COUNT * 2] | (in[FIELD_COUNT * 2 + 1] << 8));
  if (sum != checksum(in, FIELD_COUNT * 2)) return false;
  FlashcardDayRecord r;
  r.day = static_cast<uint32_t>(in[0] | (in[1] << 8));
  if (r.day == 0) return false;
  uint16_t* f[FIELD_COUNT - 1];
  fields(r, f);
  for (size_t i = 0; i < FIELD_COUNT - 1; ++i) *f[i] = static_cast<uint16_t>(in[2 + i * 2] | (in[3 + i * 2] << 8));
  out = r;
  return true;
}

void recordSpaced(const uint32_t today, const SrsRating rating, const bool wasNew, const uint16_t intervalBefore,
                  const uint16_t seconds) {
  if (today == 0 || today > 0xFFFF) return;
  beginDay(today);
  FlashcardDayRecord d;
  switch (rating) {
    case SrsRating::Again:
      d.again = 1;
      break;
    case SrsRating::Hard:
      d.hard = 1;
      break;
    case SrsRating::Good:
      d.good = 1;
      break;
    case SrsRating::Easy:
      d.easy = 1;
      break;
  }
  const uint16_t correct = rating == SrsRating::Again ? 0 : 1;
  if (wasNew) d.newCards = 1;
  if (!wasNew && intervalBefore >= 1) {
    d.reviewCards = 1;
    d.reviewCorrect = correct;
  }
  if (!wasNew && intervalBefore >= MATURE_INTERVAL) {
    d.matureReviews = 1;
    d.matureCorrect = correct;
  }
  d.spacedSeconds = std::min(seconds, MAX_CARD_SECONDS);
  current.add(d);
  dirty = true;
}

void recordLinear(const uint32_t today, const uint16_t seconds) {
  if (today == 0 || today > 0xFFFF) return;
  beginDay(today);
  FlashcardDayRecord d;
  d.linearCards = 1;
  d.linearSeconds = std::min(seconds, MAX_CARD_SECONDS);
  current.add(d);
  dirty = true;
}

bool flush() {
  if (!loaded || !dirty) return true;
  recover();
  Storage.ensureDirectoryExists("/.crosspoint");

  HalFile in;
  size_t copyBytes = 0;
  if (Storage.exists(PATH)) {
    if (!Storage.openFileForRead(TAG, PATH, in)) {
      LOG_ERR(TAG, "Cannot read %s", PATH);  // never replace the history with today alone
      return false;
    }
    size_t whole = 0;
    FlashcardDayRecord last;
    const bool replaceLast = readLast(in, whole, last) && last.day == current.day;
    copyBytes = replaceLast ? whole - RECORD_BYTES : whole;
    if (!in.seek(0)) {
      LOG_ERR(TAG, "Cannot rewind %s", PATH);
      return false;
    }
  }

  HalFile out;
  if (!Storage.openFileForWrite(TAG, TMP_PATH, out)) {
    LOG_ERR(TAG, "Cannot write %s", TMP_PATH);
    return false;
  }
  uint8_t buf[RECORD_BYTES * 8];
  bool ok = true;
  for (size_t done = 0; ok && done < copyBytes;) {
    const size_t want = std::min(sizeof(buf), copyBytes - done);
    const int n = in.read(buf, want);
    ok = n == static_cast<int>(want) && out.write(buf, want) == want;
    done += want;
  }
  encode(current, buf);
  ok = ok && out.write(buf, RECORD_BYTES) == RECORD_BYTES;
  out.close();
  if (in) in.close();  // before replacing the file; never opened when there was no history yet
  if (!ok) {
    LOG_ERR(TAG, "Write failed, keeping the old history");
    Storage.remove(TMP_PATH);
    return false;
  }
  if (Storage.exists(PATH) && !Storage.remove(PATH)) {
    LOG_ERR(TAG, "Cannot replace %s", PATH);
    return false;
  }
  if (!Storage.rename(TMP_PATH, PATH)) {
    LOG_ERR(TAG, "Rename failed; restored on next access");
    return false;
  }
  dirty = false;
  return true;
}

std::vector<FlashcardDayRecord> readRange(const uint32_t fromDay, const uint32_t toDay) {
  std::vector<FlashcardDayRecord> out;
  if (toDay < fromDay) return out;
  out.reserve(std::min<uint32_t>(toDay - fromDay + 1, 400));
  const auto merge = [&out, fromDay, toDay](const FlashcardDayRecord& r) {
    if (r.day < fromDay || r.day > toDay) return;
    if (out.empty() || r.day > out.back().day) {
      out.push_back(r);
      return;
    }
    auto it = std::lower_bound(out.begin(), out.end(), r.day,
                               [](const FlashcardDayRecord& a, const uint32_t day) { return a.day < day; });
    if (it != out.end() && it->day == r.day) {
      it->add(r);
    } else {
      out.insert(it, r);
    }
  };
  forEachRecord([&](const FlashcardDayRecord& r) {
    // Today's record from the file is superseded by the one in RAM.
    if (loaded && dirty && r.day == current.day) return;
    merge(r);
  });
  if (loaded && dirty) merge(current);
  return out;
}

Summary summarize(const uint32_t today, const uint8_t freeDaysMask) {
  struct Active {
    uint32_t day;
    uint32_t cards;
  };
  std::vector<Active> days;
  const auto addDay = [&days](const FlashcardDayRecord& r) {
    if (r.cards() > 0) days.push_back(Active{r.day, r.cards()});
  };
  forEachRecord([&](const FlashcardDayRecord& r) {
    if (loaded && dirty && r.day == current.day) return;
    addDay(r);
  });
  if (loaded && dirty) addDay(current);

  std::sort(days.begin(), days.end(), [](const Active& a, const Active& b) { return a.day < b.day; });
  // Merge days that appear twice (clock set back)
  size_t n = 0;
  for (size_t i = 0; i < days.size(); ++i) {
    if (n > 0 && days[n - 1].day == days[i].day) {
      days[n - 1].cards += days[i].cards;
    } else {
      days[n++] = days[i];
    }
  }
  days.resize(n);

  Summary s;
  if (days.empty()) return s;
  s.firstDay = days.front().day;
  uint16_t run = 0;
  for (size_t i = 0; i < days.size(); ++i) {
    if (days[i].cards > s.bestDayCards) {
      s.bestDayCards = days[i].cards;
      s.bestDay = days[i].day;
    }
    run = (i > 0 && allFree(days[i - 1].day, days[i].day, freeDaysMask)) ? static_cast<uint16_t>(run + 1) : 1;
    s.longestStreak = std::max(s.longestStreak, run);
  }
  const uint32_t last = days.back().day;
  // Today without study yet does not break the streak.
  if (last >= today || allFree(last, today, freeDaysMask)) s.currentStreak = run;
  return s;
}

void resetForTest() {
  current = FlashcardDayRecord{};
  loaded = false;
  dirty = false;
}

}  // namespace FlashcardHistory
