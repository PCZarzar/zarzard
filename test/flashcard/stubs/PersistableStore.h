#pragma once
// Host stub of the firmware JSON store: same interface, files under
// HalStorage::root, real ArduinoJson (header-only).
#include <ArduinoJson.h>
#include <HalStorage.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

class PersistableStoreBase {
 protected:
  void requestResave() {}

 public:
  static bool writeDocToFile(const char* path, const JsonDocument& doc) {
    if (doc.overflowed()) return false;  // like the firmware: never write an incomplete document
    std::string json;
    serializeJson(doc, json);
    const std::filesystem::path host(Storage.host(path));
    std::filesystem::create_directories(host.parent_path());
    std::ofstream out(host, std::ios::binary);
    out << json;
    return static_cast<bool>(out);
  }
  static bool readDocFromFile(const char* path, JsonDocument& doc) {
    std::ifstream in(Storage.host(path), std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    return !deserializeJson(doc, ss.str());
  }
};

template <typename T>
class PersistableStore : public PersistableStoreBase {
 public:
  static T& getInstance() {
    static T instance;
    return instance;
  }
  bool saveToFile() const {
    JsonDocument doc;
    static_cast<const T*>(this)->toJson(doc);
    return writeDocToFile(T::getFilePath(), doc);
  }
  bool loadFromFile() {
    JsonDocument doc;
    if (!readDocFromFile(T::getFilePath(), doc)) return false;
    return static_cast<T*>(this)->fromJson(doc.as<JsonVariantConst>());
  }
};
