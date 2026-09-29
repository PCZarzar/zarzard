#include "FlashcardPeriods.h"

#include <algorithm>

#include "FlashcardClock.h"
#include "FlashcardSrs.h"

namespace FlashcardPeriods {

uint32_t weekStart(const uint32_t day) {
  const uint32_t sinceMonday = (FlashcardClock::weekday(day) + 6u) % 7u;
  return day >= sinceMonday ? day - sinceMonday : 0;
}

uint32_t monthStart(const uint32_t day) {
  const struct tm t = FlashcardSrs::daysToTm(day);
  const uint32_t intoMonth = static_cast<uint32_t>(t.tm_mday - 1);
  return day >= intoMonth ? day - intoMonth : 0;
}

uint32_t periodStart(const Scale scale, const uint32_t day) {
  switch (scale) {
    case Scale::Weeks:
      return weekStart(day);
    case Scale::Months:
      return monthStart(day);
    default:
      return day;
  }
}

uint32_t nextStart(const Scale scale, const uint32_t start) {
  switch (scale) {
    case Scale::Weeks:
      return start + 7;
    case Scale::Months:
      return monthStart(start + 31);  // the 1st plus 31 days always lands in the next month
    default:
      return start + 1;
  }
}

uint32_t previousStart(const Scale scale, const uint32_t start) {
  if (start == 0) return 0;
  switch (scale) {
    case Scale::Weeks:
      return start >= 7 ? start - 7 : 0;
    case Scale::Months:
      return monthStart(start - 1);
    default:
      return start - 1;
  }
}

std::vector<Bucket> buckets(const Scale scale, const uint32_t today, const int count, const int pagesBack) {
  std::vector<Bucket> out;
  if (count <= 0) return out;
  uint32_t last = periodStart(scale, today);
  for (int i = 0; i < pagesBack * count; ++i) last = previousStart(scale, last);
  uint32_t first = last;
  for (int i = 1; i < count; ++i) first = previousStart(scale, first);
  out.reserve(static_cast<size_t>(count));
  uint32_t s = first;
  for (int i = 0; i < count; ++i) {
    Bucket b;
    b.start = s;
    s = nextStart(scale, s);
    b.end = s - 1;
    out.push_back(b);
  }
  return out;
}

void fill(std::vector<Bucket>& buckets, const std::vector<FlashcardDayRecord>& records) {
  for (const auto& r : records) {
    for (auto& b : buckets) {
      if (r.day >= b.start && r.day <= b.end) {
        b.sum.add(r);
        break;
      }
    }
  }
}

FlashcardDayRecord sum(const std::vector<FlashcardDayRecord>& records, const uint32_t from, const uint32_t to) {
  FlashcardDayRecord out;
  for (const auto& r : records) {
    if (r.day >= from && r.day <= to) out.add(r);
  }
  return out;
}

uint32_t daysCounted(const uint32_t start, const uint32_t end, const uint32_t today, const uint32_t firstDay) {
  if (firstDay == 0) return 0;
  const uint32_t from = std::max(start, firstDay);
  const uint32_t to = std::min(end, today);
  return to >= from ? to - from + 1 : 0;
}

int percent(const uint32_t part, const uint32_t whole) {
  if (whole == 0) return -1;
  return static_cast<int>((static_cast<uint64_t>(part) * 100 + whole / 2) / whole);
}

}  // namespace FlashcardPeriods
