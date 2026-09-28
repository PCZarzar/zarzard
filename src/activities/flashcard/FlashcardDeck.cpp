#include "FlashcardDeck.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "FlashcardCsv.h"
#include "FlashcardMemory.h"

static constexpr const char* TAG = "FlashcardDeck";
static constexpr char CSV_HEADER[] = "card_id,front_content,back_content,sr_due,sr_interval,sr_ease,flags\n";

// ---------------------------------------------------------------------------
// FlashcardCard helpers
// ---------------------------------------------------------------------------

// "/n" (the CSV-encoded newline) becomes '\n'. "\/" is a literal '/' that
// never starts a newline, so paths like /etc\/nsswitch.conf can be written.
std::string FlashcardDeck::decodeNewlines(const std::string& raw) {
  std::string result;
  result.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '/') {
      result += '/';
      ++i;
    } else if (raw[i] == '/' && i + 1 < raw.size() && raw[i + 1] == 'n') {
      result += '\n';
      ++i;
    } else {
      result += raw[i];
    }
  }
  return result;
}

static void trimWhitespace(std::string& s) {
  size_t b = 0;
  while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
  size_t e = s.size();
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
  s = s.substr(b, e - b);
}

// Single left-to-right pass over frontContent:
//   "\\"          -> literal '\' (never a hint delimiter)
//   "\ "          -> opens the hint (outside a hint)
//   " \"          -> closes the hint (inside a hint); anything after is ignored,
//                    as the old find()-based parser did
//   any other '\' -> literal
// An opener with no matching closer is rendered literally.
void FlashcardCard::parseFront(std::string& mainOut, std::string& hintOut) const {
  const std::string& s = frontContent;
  std::string main;
  std::string hint;
  bool inHint = false;
  bool closed = false;
  main.reserve(s.size());

  for (size_t i = 0; i < s.size() && !closed; ++i) {
    const char c = s[i];
    std::string& out = inHint ? hint : main;
    if (c != '\\') {
      out += c;
      continue;
    }
    const char next = (i + 1 < s.size()) ? s[i + 1] : '\0';
    if (next == '\\') {
      out += '\\';
      ++i;
    } else if (!inHint && next == ' ') {
      inHint = true;
      ++i;
    } else if (inHint && !hint.empty() && hint.back() == ' ') {
      closed = true;
    } else {
      out += '\\';
    }
  }

  if (inHint && !closed) {
    // Unterminated hint: the opener was a literal "\ ".
    main += "\\ ";
    main += hint;
    hint.clear();
  }

  trimWhitespace(main);
  trimWhitespace(hint);
  mainOut = FlashcardDeck::decodeNewlines(main);
  hintOut = FlashcardDeck::decodeNewlines(hint);
}

std::string FlashcardCard::frontMain() const {
  std::string main, hint;
  parseFront(main, hint);
  return main;
}

std::string FlashcardCard::frontHint() const {
  std::string main, hint;
  parseFront(main, hint);
  return hint;
}

// ---------------------------------------------------------------------------
// CSV parsing
// ---------------------------------------------------------------------------

// Write a field, quoting it if it contains commas, newlines, or double quotes.
void FlashcardDeck::writeCsvField(std::string& out, const std::string& field) {
  bool needsQuote = (field.find(',') != std::string::npos || field.find('"') != std::string::npos ||
                     field.find('\n') != std::string::npos || field.find('\r') != std::string::npos);
  if (!needsQuote) {
    out += field;
    return;
  }
  out += '"';
  for (char c : field) {
    if (c == '"') out += '"';  // escape by doubling
    out += c;
  }
  out += '"';
}

// ---------------------------------------------------------------------------
// FlashcardDeck — load / save
// ---------------------------------------------------------------------------

namespace {
// Counts the records of a CSV (all of them) and checks the whole file was read.
bool countRecords(const char* path, size_t& count) {
  HalFile file;
  if (!Storage.openFileForRead(TAG, path, file)) {
    LOG_ERR(TAG, "Failed to open CSV: %s", path);
    return false;
  }
  const size_t fsize = file.size();
  if (fsize == 0) {
    LOG_ERR(TAG, "Empty CSV: %s", path);
    return false;
  }
  FlashcardCsvReader reader(file);
  FlashcardCsvReader::Record rec;
  reader.skipHeader();
  count = 0;
  while (reader.next(rec)) ++count;
  if (reader.failed() || reader.bytesRead() != fsize) {
    // A partial read would load (and later save) a deck missing its tail.
    LOG_ERR(TAG, "Short read (%u of %u bytes): %s", (unsigned)reader.bytesRead(), (unsigned)fsize, path);
    return false;
  }
  return true;
}
}  // namespace

bool FlashcardDeck::loadFromCsv(const char* path) {
  cards.clear();
  cards.shrink_to_fit();
  truncated = false;
  oom = false;
  filePath = path;
  name = displayNameForPath(filePath);

  // Pass 1: count, so the card vector is allocated once at its exact size.
  size_t count = 0;
  if (!countRecords(path, count)) return false;
  const size_t keep = std::min(count, MAX_CARDS_PER_DECK);
  if (flashcardLargestFreeBlock() < keep * sizeof(FlashcardCard) + FLASHCARD_HEAP_RESERVE) {
    LOG_ERR(TAG, "Not enough memory for %u cards: %s", (unsigned)keep, path);
    oom = true;
    return false;
  }
  cards.reserve(keep);

  // Pass 2: build the cards. Strings are copied at their exact size.
  HalFile file;
  if (!Storage.openFileForRead(TAG, path, file)) return false;
  FlashcardCsvReader reader(file);
  FlashcardCsvReader::Record rec;
  reader.skipHeader();
  uint16_t nextId = 1;
  while (reader.next(rec)) {
    if (cards.size() >= MAX_CARDS_PER_DECK) {
      LOG_ERR(TAG, "Deck %s exceeds MAX_CARDS_PER_DECK (%u), truncating", path, (unsigned)MAX_CARDS_PER_DECK);
      truncated = true;
      break;
    }
    if (flashcardLargestFreeBlock() < FLASHCARD_HEAP_RESERVE) {
      LOG_ERR(TAG, "Out of memory after %u cards: %s", (unsigned)cards.size(), path);
      cards.clear();
      cards.shrink_to_fit();
      oom = true;
      return false;
    }
    FlashcardCard card;
    card.id = rec.id.empty() ? nextId : (uint16_t)atoi(rec.id.c_str());
    nextId = card.id + 1;
    card.frontContent = std::string(rec.front);
    card.backContent = std::string(rec.back);
    card.srs = rec.srs();
    card.flags = rec.flagBits();
    card.offset = rec.offset;
    cards.push_back(std::move(card));
  }
  if (reader.failed()) {
    LOG_ERR(TAG, "Read error: %s", path);
    cards.clear();
    return false;
  }
  LOG_DBG(TAG, "Loaded %u cards from %s", (unsigned)cards.size(), path);
  return true;
}

bool FlashcardDeck::scanStats(const char* path, const uint32_t today, DeckStats& out, bool& isTruncated) {
  out = DeckStats{};
  isTruncated = false;
  HalFile file;
  if (!Storage.openFileForRead(TAG, path, file)) return false;
  const size_t fsize = file.size();
  if (fsize == 0) return false;
  FlashcardCsvReader reader(file);
  FlashcardCsvReader::Record rec;
  reader.skipHeader();
  while (reader.next(rec)) {
    if (out.totalCards >= MAX_CARDS_PER_DECK) {
      isTruncated = true;
      break;
    }
    ++out.totalCards;
    const uint8_t flags = rec.flagBits();
    if (flags & FlashcardCard::FLAG_MARKED) ++out.markedCards;
    if (flags & FlashcardCard::FLAG_SUSPENDED) {
      ++out.suspendedCards;
      continue;
    }
    const SrsState srs = rec.srs();
    if (srs.dueDate == 0) {
      ++out.newCards;
    } else if (srs.dueDate <= today) {
      ++out.dueCards;
    }
  }
  if (reader.failed()) return false;
  if (!isTruncated && reader.bytesRead() != fsize) return false;
  return true;
}

std::string FlashcardDeck::displayNameForPath(const std::string& path) {
  const size_t sep = path.rfind('/');
  return fileNameToDisplayName(sep == std::string::npos ? path : path.substr(sep + 1));
}

bool FlashcardDeck::saveToCsv(const char* path) const {
  if (truncated) {
    LOG_ERR(TAG, "Not saving truncated deck (> %u cards): %s", (unsigned)MAX_CARDS_PER_DECK, path);
    return false;
  }
  // Stream the CSV directly to the SD card one line at a time. The previous
  // implementation built the whole file in a std::string and then copied it
  // into an Arduino String before writing — under -fno-exceptions this
  // aborted (→ reboot) on larger decks when free heap was already consumed
  // by two font caches. ~200 bytes of stack + a 2KB line buffer is enough.
  std::string tmpPath = std::string(path) + ".tmp";

  // A previous save that failed after removing the CSV left the only complete
  // copy in the .tmp; put it back before the .tmp is truncated below.
  if (!Storage.exists(path) && Storage.exists(tmpPath.c_str())) {
    if (!Storage.rename(tmpPath.c_str(), path)) {
      LOG_ERR(TAG, "Cannot restore %s, not overwriting it", tmpPath.c_str());
      return false;
    }
  }

  HalFile out;
  if (!Storage.openFileForWrite(TAG, tmpPath, out)) {
    LOG_ERR(TAG, "Failed to open tmp file: %s", tmpPath.c_str());
    return false;
  }

  auto writeAll = [&](const char* data, size_t len) -> bool {
    return out.write(reinterpret_cast<const uint8_t*>(data), len) == len;
  };
  auto writeStr = [&](const std::string& s) -> bool { return writeAll(s.data(), s.size()); };

  bool ok = writeAll(CSV_HEADER, sizeof(CSV_HEADER) - 1);

  std::string lineField;
  char dateBuf[12];
  char numBuf[16];
  for (const auto& card : cards) {
    if (!ok) break;
    if (card.flags & FlashcardCard::FLAG_DELETED) continue;
    int n = snprintf(numBuf, sizeof(numBuf), "%u,", (unsigned)card.id);
    ok = ok && writeAll(numBuf, (size_t)n);

    lineField.clear();
    writeCsvField(lineField, card.frontContent);
    ok = ok && writeStr(lineField);
    ok = ok && writeAll(",", 1);

    lineField.clear();
    writeCsvField(lineField, card.backContent);
    ok = ok && writeStr(lineField);
    ok = ok && writeAll(",", 1);

    if (card.srs.dueDate != 0) {
      FlashcardSrs::formatDateStr(card.srs.dueDate, dateBuf, sizeof(dateBuf));
      n = snprintf(numBuf, sizeof(numBuf), "%s,", dateBuf);
      ok = ok && writeAll(numBuf, (size_t)n);
      n = snprintf(numBuf, sizeof(numBuf), "%u,", (unsigned)card.srs.interval);
      ok = ok && writeAll(numBuf, (size_t)n);
      n = snprintf(numBuf, sizeof(numBuf), "%u", (unsigned)card.srs.ease);
      ok = ok && writeAll(numBuf, (size_t)n);
    } else {
      ok = ok && writeAll(",,", 2);
    }
    char flags[4] = ",";
    size_t flagLen = 1;
    if (card.isMarked()) flags[flagLen++] = 'm';
    if (card.isSuspended()) flags[flagLen++] = 's';
    ok = ok && writeAll(flags, flagLen);
    ok = ok && writeAll("\n", 1);
  }

  out.close();
  if (!ok) {
    Storage.remove(tmpPath.c_str());
    LOG_ERR(TAG, "Failed streaming write: %s", tmpPath.c_str());
    return false;
  }

  // Try rename directly (FAT rename replaces destination atomically on most impls).
  // If that fails, fall back to remove-then-rename.
  if (!Storage.rename(tmpPath.c_str(), path)) {
    Storage.remove(path);
    if (!Storage.rename(tmpPath.c_str(), path)) {
      LOG_ERR(TAG, "Failed to rename %s -> %s", tmpPath.c_str(), path);
      return false;
    }
  }

  LOG_DBG(TAG, "Saved %u cards to %s", (unsigned)cards.size(), path);
  return true;
}

// ---------------------------------------------------------------------------
// FlashcardDeck — queries
// ---------------------------------------------------------------------------

DeckStats FlashcardDeck::getStats(uint32_t today) const {
  DeckStats stats;
  for (const auto& card : cards) {
    if (card.flags & FlashcardCard::FLAG_DELETED) continue;
    ++stats.totalCards;
    if (card.isMarked()) ++stats.markedCards;
    if (card.isSuspended()) {
      ++stats.suspendedCards;
      continue;
    }
    if (card.isNew()) {
      ++stats.newCards;
    } else if (card.isDue(today)) {
      ++stats.dueCards;
    }
  }
  return stats;
}

std::vector<size_t> FlashcardDeck::buildReviewQueue(uint32_t today, uint8_t newPerDay, uint16_t maxReview) const {
  std::vector<size_t> due;
  std::vector<size_t> newCards;
  due.reserve(cards.size());
  newCards.reserve(cards.size());

  for (size_t i = 0; i < cards.size(); ++i) {
    const FlashcardCard& c = cards[i];
    if (!c.isActive()) continue;
    if (c.isNew()) {
      newCards.push_back(i);
    } else if (c.isDue(today)) {
      due.push_back(i);
    }
  }

  std::vector<size_t> queue;
  queue.reserve(std::min((size_t)maxReview, due.size() + newCards.size()));

  // Due cards first
  for (size_t idx : due) {
    if (queue.size() >= maxReview) break;
    queue.push_back(idx);
  }

  // Then new cards, capped at newPerDay
  uint8_t newAdded = 0;
  for (size_t idx : newCards) {
    if (queue.size() >= maxReview) break;
    if (newAdded >= newPerDay) break;
    queue.push_back(idx);
    ++newAdded;
  }

  return queue;
}

uint16_t FlashcardDeck::newCardsAvailable(uint8_t newPerDay, uint16_t& todayCount, uint32_t& todayDate,
                                          uint32_t today) {
  if (today == 0) return newPerDay;
  if (todayDate != today) {
    todayDate = today;
    todayCount = 0;
  }
  return todayCount >= newPerDay ? 0 : (uint16_t)(newPerDay - todayCount);
}

uint16_t FlashcardDeck::spacedSessionSize(const DeckStats& stats, uint16_t newQuota, uint16_t maxReview) {
  const uint32_t total = (uint32_t)stats.dueCards + std::min<uint32_t>(stats.newCards, newQuota);
  return (uint16_t)std::min<uint32_t>(total, maxReview);
}

uint32_t FlashcardDeck::contentFingerprint() const {
  std::vector<size_t> all(cards.size());
  for (size_t i = 0; i < cards.size(); ++i) all[i] = i;
  return fingerprintOf(all);
}

uint32_t FlashcardDeck::fingerprintOf(const std::vector<size_t>& indices) const {
  uint32_t h = 2166136261u;  // FNV-1a
  auto mix = [&h](const void* data, size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
      h ^= p[i];
      h *= 16777619u;
    }
  };
  for (const size_t i : indices) {
    if (i >= cards.size()) continue;
    const auto& c = cards[i];
    mix(&c.id, sizeof(c.id));
    mix(c.frontContent.data(), c.frontContent.size() + 1);  // include the NUL as a field separator
    mix(c.backContent.data(), c.backContent.size() + 1);
  }
  return h;
}

std::vector<size_t> FlashcardDeck::activeIndices() const {
  std::vector<size_t> out;
  out.reserve(cards.size());
  for (size_t i = 0; i < cards.size(); ++i) {
    if (cards[i].isActive()) out.push_back(i);
  }
  return out;
}

int FlashcardDeck::findByOffset(const uint32_t offset) const {
  for (size_t i = 0; i < cards.size(); ++i) {
    if (cards[i].offset == offset && !(cards[i].flags & FlashcardCard::FLAG_DELETED)) return static_cast<int>(i);
  }
  return -1;
}

bool FlashcardDeck::updateCard(size_t index, const SrsState& newState) {
  if (index >= cards.size()) {
    LOG_ERR(TAG, "updateCard: index %u out of range (%u)", (unsigned)index, (unsigned)cards.size());
    return false;
  }
  cards[index].srs = newState;
  return true;
}

// ---------------------------------------------------------------------------
// FlashcardDeck — static helpers
// ---------------------------------------------------------------------------

// Case-insensitive ".csv" suffix test — SdFat can surface either case.
static bool hasCsvSuffix(const std::string& fname) {
  if (fname.size() <= 4) return false;
  const std::string tail = fname.substr(fname.size() - 4);
  return (tail == ".csv" || tail == ".CSV" || tail == ".Csv" || tail == ".cSv" || tail == ".csV" || tail == ".CSv" ||
          tail == ".cSV" || tail == ".CsV");
}

std::vector<std::string> FlashcardDeck::listFolders() {
  std::vector<std::string> result;
  HalFile dir = Storage.open(FLASHCARD_DIR);
  if (!dir) return result;
  result.reserve(8);
  dir.rewindDirectory();
  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) continue;
    char nameBuf[128];
    entry.getName(nameBuf, sizeof(nameBuf));
    if (nameBuf[0] == '\0' || nameBuf[0] == '.') continue;
    result.emplace_back(nameBuf);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<std::string> FlashcardDeck::listDecks(const std::string& dirPath) {
  std::vector<std::string> result;

  HalFile dir = Storage.open(dirPath.c_str());
  if (!dir) {
    LOG_DBG(TAG, "listDecks: directory not found: %s", dirPath.c_str());
    return result;
  }

  std::vector<std::string> orphanTmps;  // "x.csv.tmp" left by an interrupted save
  result.reserve(16);
  dir.rewindDirectory();
  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      char nameBuf[128];
      entry.getName(nameBuf, sizeof(nameBuf));
      std::string fname(nameBuf);
      if (fname.empty() || fname[0] == '.') continue;  // hidden, e.g. macOS "._deck.csv"
      if (hasCsvSuffix(fname)) {
        result.push_back(dirPath + "/" + fname);
      } else if (fname.size() > 4 && fname.compare(fname.size() - 4, 4, ".tmp") == 0 &&
                 hasCsvSuffix(fname.substr(0, fname.size() - 4))) {
        orphanTmps.push_back(fname.substr(0, fname.size() - 4));
      }
    }
  }
  dir.close();  // before renaming entries of this directory

  // saveToCsv() removes the old CSV before renaming the .tmp into place; a
  // power loss in between leaves only the .tmp. Restore it.
  for (const auto& csvName : orphanTmps) {
    const std::string csvPath = dirPath + "/" + csvName;
    if (std::find(result.begin(), result.end(), csvPath) != result.end()) continue;
    const std::string tmpPath = csvPath + ".tmp";
    if (Storage.rename(tmpPath.c_str(), csvPath.c_str())) {
      LOG_INF(TAG, "Recovered interrupted save: %s", csvPath.c_str());
      result.push_back(csvPath);
    } else {
      LOG_ERR(TAG, "Failed to recover %s", tmpPath.c_str());
    }
  }

  // Sort alphabetically for consistent ordering
  std::sort(result.begin(), result.end());
  return result;
}

std::string FlashcardDeck::fileNameToDisplayName(const std::string& filename) {
  // Strip .csv extension
  std::string name = filename;
  if (hasCsvSuffix(name)) {
    name.resize(name.size() - 4);
  }
  // Replace underscores with spaces
  for (char& c : name) {
    if (c == '_') c = ' ';
  }
  return name;
}
