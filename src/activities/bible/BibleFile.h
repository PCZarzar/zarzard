#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A Bible version on the SD card: /biblia/<name>.zbib, made on the computer by
// tools/make_zbib.py. Only the header and the book table stay in memory; a
// chapter is read when it is opened.
namespace Bible {

constexpr const char* DIR = "/biblia";

struct Book {
  std::string name;
  std::string abbrev;
  uint16_t firstChapter = 0;  // index of its first chapter in the whole Bible
  uint16_t chapters = 0;
};

struct Version {
  std::string path;
  std::string label;     // "ARA"
  std::string fullName;  // "Almeida Revista e Atualizada"
};

// Valid .zbib files in /biblia, sorted by file name.
std::vector<Version> listVersions();

class File {
 public:
  // Reads and checks the header and the book table.
  bool open(const std::string& path);
  const std::vector<Book>& books() const { return bookList; }
  const std::string& label() const { return versionLabel; }
  // Verses of one chapter, each ending in '\n'.
  bool readChapter(int book, int chapter, std::string& text) const;

 private:
  std::string filePath;
  std::string versionLabel;
  std::vector<Book> bookList;
  uint16_t chapterCount = 0;
  uint32_t chapterTable = 0;  // file offset of the chapter table
  uint32_t fileSize = 0;
};

// Where reading stopped in each version, and the version used last.
struct Position {
  uint16_t book = 0;
  uint16_t chapter = 0;
  uint16_t verse = 1;
  uint16_t line = 0;  // lines into that verse where the page starts (long verses span pages)
  std::string where;  // "Salmos 23", shown on the version list
};
bool loadPosition(const std::string& label, Position& out);
std::string loadLastVersion();
bool savePosition(const std::string& label, const Position& pos);

}  // namespace Bible
