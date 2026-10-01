#include "FlashcardLinearProgress.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstdio>
#include <vector>

namespace FlashcardLinearProgress {

namespace {
constexpr uint8_t FORMAT_VERSION = 1;

std::string progressPath(const std::string& deckPath) {
  uint32_t h = 2166136261u;  // FNV-1a of the deck path
  for (const char c : deckPath) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  char buf[48];
  snprintf(buf, sizeof(buf), "/.crosspoint/fc_linear_%08lx.json", static_cast<unsigned long>(h));
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
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(progressPath(deckPath).c_str(), doc)) return false;
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
  const bool ok = PersistableStoreBase::writeDocToFile(progressPath(deckPath).c_str(), doc);
  if (!ok) LOG_ERR("FCLP", "Failed to save linear progress for %s", deckPath.c_str());
  return ok;
}

void clear(const std::string& deckPath) {
  const std::string path = progressPath(deckPath);
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
}

}  // namespace FlashcardLinearProgress
