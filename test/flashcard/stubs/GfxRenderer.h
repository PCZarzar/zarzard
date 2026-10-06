#pragma once
// Host stub: measures text with the real built-in EpdFont data so layout
// decisions match the device. Only the calls FlashcardLayout uses.
#include <EpdFontFamily.h>

#include <map>

class GfxRenderer {
 public:
  void insertFont(int id, const EpdFontFamily& f) { fonts.emplace(id, f); }
  int getTextWidth(int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    if (text == nullptr || *text == '\0') return 0;
    int w = 0, h = 0;
    fonts.at(fontId).getTextDimensions(text, &w, &h, style);
    return w;
  }
  int getLineHeight(int fontId) const { return fonts.at(fontId).getData()->advanceY; }
  bool fontHasCodepoint(int fontId, uint32_t cp) const {
    const auto it = fonts.find(fontId);
    return it != fonts.end() && it->second.hasCodepoint(cp);
  }

 private:
  std::map<int, EpdFontFamily> fonts;
};
