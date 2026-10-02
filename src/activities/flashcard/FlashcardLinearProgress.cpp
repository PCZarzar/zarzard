#include "FlashcardLinearProgress.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace FlashcardLinearProgress {

namespace {
constexpr uint8_t FORMAT_VERSION = 1;

constexpr const char* SELECTION_SUFFIX = "/@selection";

uint32_t fnv1a(const std::string& s) {
  uint32_t h = 2166136261u;
  for (const char c : s) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  return h;
}

std::string progressPath(const std::string& deckPath) {
  char buf[48];
  snprintf(buf, sizeof(buf), "/.crosspoint/fc_linear_%08lx.json", static_cast<unsigned long>(fnv1a(deckPath)));
  return buf;
}

std::string selectionPath(const std::string& folderDir) {
  char buf[48];
  snprintf(buf, sizeof(buf), "/.crosspoint/fc_select_%08lx.json", static_cast<unsigned long>(fnv1a(folderDir)));
  return buf;
}

void writeArray(JsonDocument& doc, const char* key, const std::vector<size_t>& values) {
  JsonArray arr = doc[key].to<JsonArray>();
  for (const size_t v : values) arr.add(static_cast<uint32_t>(v));
}

bool readArray(const JsonDocument& doc, const char* key, std::vector<size_t>& out) {
  JsonArrayConst arr = doc[key].as<JsonArrayConst>();
  if (arr.isNull()) return false;
  out.clear();
  out.reserve(arr.size());
  for (JsonVariantConst v : arr) {
    if (!v.is<uint32_t>()) return false;
    out.push_back(v.as<uint32_t>());
  }
  return true;
}
}  // namespace

bool load(const std::string& deckPath, Saved& out) {
  const std::string path = progressPath(deckPath);
  const std::string tmp = path + ".tmp";
  // A save interrupted between removing the old file and the rename left the
  // new checkpoint only in the .tmp: take it.
  if (!Storage.exists(path.c_str()) && Storage.exists(tmp.c_str())) Storage.rename(tmp.c_str(), path.c_str());
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), doc)) return false;
  // Guards against a hash collision between two deck paths.
  if ((doc["v"] | static_cast<uint8_t>(0)) != FORMAT_VERSION || deckPath != (doc["deck"] | "")) return false;
  out.fingerprint = doc["fp"] | static_cast<uint32_t>(0);
  out.state.pass = doc["pass"] | static_cast<uint16_t>(0);
  out.state.position = doc["pos"] | static_cast<uint32_t>(0);
  return readArray(doc, "queue", out.state.queue) && readArray(doc, "missed", out.state.missed) &&
         readArray(doc, "unsure", out.state.unsure) && readArray(doc, "known", out.state.known);
}

bool save(const std::string& deckPath, const Saved& saved) {
  JsonDocument doc;
  doc["v"] = FORMAT_VERSION;
  doc["deck"] = deckPath.c_str();
  doc["fp"] = saved.fingerprint;
  doc["pass"] = saved.state.pass;
  doc["pos"] = static_cast<uint32_t>(saved.state.position);
  writeArray(doc, "queue", saved.state.queue);
  writeArray(doc, "missed", saved.state.missed);
  writeArray(doc, "unsure", saved.state.unsure);
  writeArray(doc, "known", saved.state.known);
  if (doc.overflowed()) {
    LOG_ERR("FCLP", "Not enough memory to save linear progress: %s", deckPath.c_str());
    return false;  // Preserve the last valid checkpoint.
  }
  // Written to a .tmp, read back, then renamed over the old file: the last
  // good checkpoint survives a failed write or a power cut (load() recovers a
  // lone .tmp).
  const std::string path = progressPath(deckPath);
  const std::string tmp = path + ".tmp";
  JsonDocument check;
  bool ok = PersistableStoreBase::writeDocToFile(tmp.c_str(), doc) &&
            PersistableStoreBase::readDocFromFile(tmp.c_str(), check) &&
            (check["pos"] | static_cast<uint32_t>(UINT32_MAX)) == static_cast<uint32_t>(saved.state.position);
  if (ok && !Storage.rename(tmp.c_str(), path.c_str())) {
    Storage.remove(path.c_str());
    ok = Storage.rename(tmp.c_str(), path.c_str());
  }
  if (!ok) {
    if (Storage.exists(tmp.c_str()) && Storage.exists(path.c_str())) Storage.remove(tmp.c_str());
    LOG_ERR("FCLP", "Failed to save linear progress for %s", deckPath.c_str());
  }
  return ok;
}

void clear(const std::string& deckPath) {
  const std::string path = progressPath(deckPath);
  const std::string tmp = path + ".tmp";
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  if (Storage.exists(tmp.c_str())) Storage.remove(tmp.c_str());
}

std::string selectionKey(const std::string& folderDir) { return folderDir + SELECTION_SUFFIX; }

bool isSelectionKey(const std::string& key, std::string& folderDirOut) {
  const size_t n = strlen(SELECTION_SUFFIX);
  if (key.size() <= n || key.compare(key.size() - n, n, SELECTION_SUFFIX) != 0) return false;
  folderDirOut = key.substr(0, key.size() - n);
  return true;
}

bool saveSelection(const std::string& folderDir, const std::vector<std::string>& deckPaths) {
  JsonDocument doc;
  doc["v"] = FORMAT_VERSION;
  doc["folder"] = folderDir.c_str();
  JsonArray arr = doc["decks"].to<JsonArray>();
  for (const auto& p : deckPaths) arr.add(p.c_str());
  const bool ok = PersistableStoreBase::writeDocToFile(selectionPath(folderDir).c_str(), doc);
  if (!ok) LOG_ERR("FCLP", "Failed to save the selection for %s", folderDir.c_str());
  return ok;
}

std::vector<std::string> loadSelection(const std::string& folderDir) {
  std::vector<std::string> out;
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(selectionPath(folderDir).c_str(), doc)) return out;
  if (folderDir != (doc["folder"] | "")) return out;  // hash collision between folders
  JsonArrayConst arr = doc["decks"].as<JsonArrayConst>();
  out.reserve(arr.size());
  for (JsonVariantConst v : arr) {
    const char* p = v.as<const char*>();
    if (p && Storage.exists(p)) out.emplace_back(p);
  }
  return out;
}

}  // namespace FlashcardLinearProgress
