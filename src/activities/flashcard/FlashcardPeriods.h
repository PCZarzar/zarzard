#pragma once
#include <cstdint>
#include <vector>

#include "FlashcardHistory.h"

// Calendar periods over day indices (days since 2000-01-01) for the statistics
// screens. Weeks start on Monday. Pure functions, tested on the host.
namespace FlashcardPeriods {

enum class Scale : uint8_t { Days, Weeks, Months };

uint32_t weekStart(uint32_t day);
uint32_t monthStart(uint32_t day);
// First day of the period of `scale` containing day.
uint32_t periodStart(Scale scale, uint32_t day);
// First day of the next / previous period, from a period start.
uint32_t nextStart(Scale scale, uint32_t start);
uint32_t previousStart(Scale scale, uint32_t start);

struct Bucket {
  uint32_t start = 0;
  uint32_t end = 0;  // last day, inclusive
  FlashcardDayRecord sum;
};

// `count` consecutive periods; the last one is `pagesBack` pages (of `count`
// periods) before the period containing `today`.
std::vector<Bucket> buckets(Scale scale, uint32_t today, int count, int pagesBack);

// Adds each record to the bucket containing its day.
void fill(std::vector<Bucket>& buckets, const std::vector<FlashcardDayRecord>& records);

// Sum of the records with from <= day <= to.
FlashcardDayRecord sum(const std::vector<FlashcardDayRecord>& records, uint32_t from, uint32_t to);

// Percentage 0..100 rounded to nearest, or -1 without data.
int percent(uint32_t part, uint32_t whole);

}  // namespace FlashcardPeriods
