#pragma once
// Host stub of the firmware SD-card HAL. Firmware paths ("/flashcard/x.csv")
// are mapped under HalStorage::root. Like the firmware (DESTRUCTOR_CLOSES_FILE=1),
// HalFile closes itself on destruction and is move-only.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(HalFile&& o) noexcept { *this = std::move(o); }
  HalFile& operator=(HalFile&& o) noexcept {
    if (this != &o) {
      close();
      fp = std::exchange(o.fp, nullptr);
      isDir = std::exchange(o.isDir, false);
      name = std::move(o.name);
      entries = std::move(o.entries);
      entryIsDir = std::move(o.entryIsDir);
      dirPos = o.dirPos;
    }
    return *this;
  }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  operator bool() const { return fp != nullptr || isDir; }
  size_t size() {
    if (!fp) return 0;
    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return (size_t)sz;
  }
  size_t fileSize() { return size(); }
  bool seek(size_t pos) { return fp && fseek(fp, (long)pos, SEEK_SET) == 0; }
  size_t position() const { return fp ? (size_t)ftell(fp) : 0; }
  bool close() {
    if (fp) fclose(fp);
    fp = nullptr;
    isDir = false;
    return true;
  }
  size_t write(const uint8_t* d, size_t n) { return fp ? fwrite(d, 1, n, fp) : 0; }
  size_t write(const void* d, size_t n) { return write(static_cast<const uint8_t*>(d), n); }
  int read(void* d, size_t n) { return fp ? (int)fread(d, 1, n, fp) : -1; }
  int available() {
    if (!fp) return 0;
    int c = fgetc(fp);
    if (c == EOF) return 0;
    ungetc(c, fp);
    return 1;
  }
  void flush() {
    if (fp) fflush(fp);
  }
  bool isDirectory() const { return isDir; }
  void rewindDirectory() { dirPos = 0; }
  HalFile openNextFile();
  size_t getName(char* buf, size_t n) {
    snprintf(buf, n, "%s", name.c_str());
    return name.size();
  }

  FILE* fp = nullptr;
  bool isDir = false;
  std::string name;
  std::vector<std::string> entries;
  std::vector<bool> entryIsDir;
  size_t dirPos = 0;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage s;
    return s;
  }
  std::string root = ".";
  std::string host(const char* p) const { return root + p; }

  bool openFileForRead(const char*, const char* path, HalFile& f) {
    f.close();
    f.fp = fopen(host(path).c_str(), "rb");
    return f.fp != nullptr;
  }
  bool openFileForRead(const char* m, const std::string& p, HalFile& f) { return openFileForRead(m, p.c_str(), f); }
  bool openFileForWrite(const char*, const char* path, HalFile& f) {
    f.close();
    f.fp = fopen(host(path).c_str(), "wb");
    return f.fp != nullptr;
  }
  bool openFileForWrite(const char* m, const std::string& p, HalFile& f) { return openFileForWrite(m, p.c_str(), f); }
  size_t readFileToBuffer(const char* path, char* buf, size_t bufSize, size_t = 0) {
    FILE* fp = fopen(host(path).c_str(), "rb");
    if (!fp) return 0;
    size_t n = fread(buf, 1, bufSize - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return n;
  }
  bool ensureDirectoryExists(const char* path) {
    std::error_code ec;
    std::filesystem::create_directories(host(path), ec);
    return std::filesystem::is_directory(host(path));
  }
  bool exists(const char* path) { return std::filesystem::exists(host(path)); }
  bool remove(const char* path) { return ::remove(host(path).c_str()) == 0; }
  // Like SdFat: fails when the destination already exists.
  bool rename(const char* a, const char* b) {
    if (std::filesystem::exists(host(b))) return false;
    return ::rename(host(a).c_str(), host(b).c_str()) == 0;
  }
  HalFile open(const char* path) {
    HalFile d;
    std::error_code ec;
    if (!std::filesystem::is_directory(host(path), ec)) return d;
    d.isDir = true;
    for (auto& e : std::filesystem::directory_iterator(host(path))) {
      d.entries.push_back(e.path().filename().string());
      d.entryIsDir.push_back(e.is_directory());
    }
    d.name = path;
    return d;
  }
};

inline HalFile HalFile::openNextFile() {
  HalFile e;
  if (dirPos >= entries.size()) return e;
  e.name = entries[dirPos];
  e.isDir = entryIsDir[dirPos];
  if (!e.isDir) e.fp = fopen((HalStorage::getInstance().root + name + "/" + e.name).c_str(), "rb");
  ++dirPos;
  return e;
}

#define Storage HalStorage::getInstance()
