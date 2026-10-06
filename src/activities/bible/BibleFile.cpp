#include "BibleFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cstring>

namespace Bible {

namespace {
constexpr const char* TAG = "BIBLE";
constexpr uint16_t FORMAT_VERSION = 1;
constexpr size_t HEADER_SIZE = 92;
constexpr size_t BOOK_SIZE = 44;
constexpr size_t CHAPTER_SIZE = 12;
constexpr uint32_t MAX_CHAPTER_BYTES = 64 * 1024;
constexpr const char* POSITION_PATH = "/.crosspoint/bible.json";

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
std::string fixedString(const uint8_t* p, size_t n) {
  size_t len = 0;
  while (len < n && p[len] != 0) ++len;
  return std::string(reinterpret_cast<const char*>(p), len);
}
bool readAll(HalFile& f, void* buf, size_t n) { return f.read(buf, n) == static_cast<int>(n); }

bool endsWith(const std::string& s, const char* suffix) {
  const size_t n = strlen(suffix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i]) return false;
  }
  return true;
}
}  // namespace

bool File::open(const std::string& path) {
  bookList.clear();
  HalFile f;
  if (!Storage.openFileForRead(TAG, path, f)) return false;
  uint8_t head[HEADER_SIZE];
  const size_t size = f.size();
  if (size < HEADER_SIZE || !readAll(f, head, sizeof(head)) || memcmp(head, "ZBIB", 4) != 0 ||
      u16(head + 4) != FORMAT_VERSION) {
    LOG_ERR(TAG, "Not a Bible file: %s", path.c_str());
    return false;
  }
  const uint16_t books = u16(head + 6);
  const uint16_t chapters = u16(head + 8);
  if (books == 0 || books > 100 || chapters == 0 || chapters > 2000) return false;
  const uint32_t table = HEADER_SIZE + BOOK_SIZE * books;
  if (size < table + CHAPTER_SIZE * chapters) return false;

  std::vector<Book> list;
  list.reserve(books);
  uint32_t next = 0;
  for (uint16_t i = 0; i < books; ++i) {
    uint8_t row[BOOK_SIZE];
    if (!readAll(f, row, sizeof(row))) return false;
    Book b;
    b.name = fixedString(row, 32);
    b.abbrev = fixedString(row + 32, 8);
    b.firstChapter = u16(row + 40);
    b.chapters = u16(row + 42);
    // Books are stored in order, their chapters one after the other.
    if (b.name.empty() || b.chapters == 0 || b.firstChapter != next) return false;
    next += b.chapters;
    list.push_back(std::move(b));
  }
  if (next != chapters) return false;

  filePath = path;
  versionLabel = fixedString(head + 12, 16);
  bookList = std::move(list);
  chapterCount = chapters;
  chapterTable = table;
  fileSize = static_cast<uint32_t>(size);
  return true;
}

bool File::readChapter(const int book, const int chapter, std::string& text) const {
  text.clear();
  if (book < 0 || book >= static_cast<int>(bookList.size())) return false;
  const Book& b = bookList[book];
  if (chapter < 0 || chapter >= b.chapters) return false;
  HalFile f;
  if (!Storage.openFileForRead(TAG, filePath, f)) return false;
  uint8_t row[CHAPTER_SIZE];
  if (!f.seek(chapterTable + CHAPTER_SIZE * (b.firstChapter + chapter)) || !readAll(f, row, sizeof(row))) {
    return false;
  }
  const uint32_t offset = u32(row);
  const uint32_t length = u32(row + 4);
  if (length == 0 || length > MAX_CHAPTER_BYTES || offset > fileSize || length > fileSize - offset) {
    LOG_ERR(TAG, "Bad chapter %d:%d in %s", book, chapter, filePath.c_str());
    return false;
  }
  text.resize(length);
  if (!f.seek(offset) || !readAll(f, &text[0], length) || text.back() != '\n') {
    text.clear();
    return false;
  }
  return true;
}

std::vector<Version> listVersions() {
  std::vector<Version> out;
  HalFile dir = Storage.open(DIR);
  if (!dir || !dir.isDirectory()) return out;
  dir.rewindDirectory();
  std::vector<std::string> names;
  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) continue;
    char name[128];
    entry.getName(name, sizeof(name));
    if (name[0] == '.' || !endsWith(name, ".zbib")) continue;
    names.emplace_back(name);
  }
  std::sort(names.begin(), names.end());
  for (const auto& name : names) {
    const std::string path = std::string(DIR) + "/" + name;
    HalFile f;
    if (!Storage.openFileForRead(TAG, path, f)) continue;
    uint8_t head[HEADER_SIZE];
    if (!readAll(f, head, sizeof(head)) || memcmp(head, "ZBIB", 4) != 0 || u16(head + 4) != FORMAT_VERSION) continue;
    Version v;
    v.path = path;
    v.label = fixedString(head + 12, 16);
    v.fullName = fixedString(head + 28, 64);
    if (v.label.empty()) v.label = name.substr(0, name.size() - 5);
    out.push_back(std::move(v));
  }
  return out;
}

namespace {
bool readPositions(JsonDocument& doc) {
  const std::string tmp = std::string(POSITION_PATH) + ".tmp";
  // A save cut between removing the old file and the rename left only the .tmp.
  if (!Storage.exists(POSITION_PATH) && Storage.exists(tmp.c_str())) Storage.rename(tmp.c_str(), POSITION_PATH);
  return PersistableStoreBase::readDocFromFile(POSITION_PATH, doc) && (doc["v"] | 0) == FORMAT_VERSION;
}
}  // namespace

bool loadPosition(const std::string& label, Position& out) {
  JsonDocument doc;
  if (!readPositions(doc)) return false;
  JsonArrayConst a = doc["pos"][label].as<JsonArrayConst>();
  if (a.size() != 5) return false;
  out.book = a[0] | static_cast<uint16_t>(0);
  out.chapter = a[1] | static_cast<uint16_t>(0);
  out.verse = a[2] | static_cast<uint16_t>(1);
  out.line = a[3] | static_cast<uint16_t>(0);
  out.where = a[4] | "";
  return true;
}

std::string loadLastVersion() {
  JsonDocument doc;
  if (!readPositions(doc)) return "";
  return doc["last"] | "";
}

bool savePosition(const std::string& label, const Position& pos) {
  JsonDocument doc;
  if (!readPositions(doc)) {
    doc.clear();
    doc["v"] = FORMAT_VERSION;
  }
  doc["last"] = label.c_str();
  JsonArray a = doc["pos"][label].to<JsonArray>();
  a.add(pos.book);
  a.add(pos.chapter);
  a.add(pos.verse);
  a.add(pos.line);
  a.add(pos.where.c_str());
  if (doc.overflowed()) return false;
  // Written to a .tmp, then renamed over the old file: a power cut keeps the
  // last good position (readPositions recovers a lone .tmp).
  const std::string tmp = std::string(POSITION_PATH) + ".tmp";
  bool ok = PersistableStoreBase::writeDocToFile(tmp.c_str(), doc);
  if (ok && !Storage.rename(tmp.c_str(), POSITION_PATH)) {
    Storage.remove(POSITION_PATH);
    ok = Storage.rename(tmp.c_str(), POSITION_PATH);
  }
  if (!ok) LOG_ERR(TAG, "Failed to save the reading position");
  return ok;
}

}  // namespace Bible
