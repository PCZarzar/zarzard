#include "FlashcardChart.h"

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"

namespace FlashcardChart {

namespace {
void fillColor(const GfxRenderer& renderer, const int x, const int y, const int w, const int h, const Color color) {
  if (w <= 0 || h <= 0) return;
  if (color == Color::Black) {
    renderer.fillRect(x, y, w, h, true);
  } else if (color != Color::White && color != Color::Clear) {
    renderer.fillRectDither(x, y, w, h, color);
  }
}
}  // namespace

void drawBars(const GfxRenderer& renderer, const Rect& area, const Bar* bars, const int count, const bool valuesOnTop) {
  if (count <= 0) return;
  const int smallH = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int markerH = 4;
  const int labelTop = area.y + area.height - smallH;
  const int baseY = labelTop - markerH - 3;
  const int topY = area.y + smallH + 4;
  const int chartH = baseY - topY;
  if (chartH < 10) return;

  uint32_t maxV = 0;
  for (int i = 0; i < count; ++i) maxV = std::max(maxV, bars[i].base + bars[i].top);

  char buf[16];
  if (maxV > 0 && !valuesOnTop) {
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(maxV));
    renderer.drawText(SMALL_FONT_ID, area.x, area.y, buf);
    for (int x = area.x; x < area.x + area.width; x += 4) renderer.drawPixel(x, topY, true);  // dotted max line
  }
  renderer.drawLine(area.x, baseY + 1, area.x + area.width - 1, baseY + 1, 2, true);

  for (int i = 0; i < count; ++i) {
    const Bar& b = bars[i];
    const int x0 = area.x + i * area.width / count;
    const int x1 = area.x + (i + 1) * area.width / count;
    const int slot = x1 - x0;
    const int barW = std::max(2, slot * 3 / 4);
    const int bx = x0 + (slot - barW) / 2;
    const uint32_t total = b.base + b.top;
    int h = 0;
    if (total > 0 && maxV > 0) {
      h = static_cast<int>(static_cast<uint64_t>(total) * chartH / maxV);
      h = std::max(h, 2);
      const int hb = static_cast<int>(static_cast<uint64_t>(h) * b.base / total);
      renderer.fillRect(bx, baseY - hb, barW, hb, true);
      if (h > hb) {
        renderer.fillRectDither(bx, baseY - h, barW, h - hb, Color::DarkGray);
        renderer.drawRect(bx, baseY - h, barW, h - hb, true);
      }
      if (valuesOnTop) {
        snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(total));
        const int tw = renderer.getTextWidth(SMALL_FONT_ID, buf);
        if (tw <= slot) renderer.drawText(SMALL_FONT_ID, x0 + (slot - tw) / 2, baseY - h - smallH - 1, buf);
      }
    }
    if (b.mark) renderer.fillRect(bx, baseY + 4, barW, markerH, true);
    if (b.label && b.label[0]) {
      const auto style = b.mark ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const int tw = renderer.getTextWidth(SMALL_FONT_ID, b.label, style);
      int tx = x0 + (slot - tw) / 2;
      tx = std::clamp(tx, area.x, area.x + area.width - tw);
      renderer.drawText(SMALL_FONT_ID, tx, labelTop, b.label, true, style);
    }
  }
}

void drawStacked(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                 const Segment* segments, const int count) {
  if (w <= 2 || h <= 2) return;
  uint64_t sum = 0;
  for (int i = 0; i < count; ++i) sum += segments[i].value;
  if (sum > 0) {
    int cx = x;
    uint64_t acc = 0;
    for (int i = 0; i < count; ++i) {
      if (segments[i].value == 0) continue;
      acc += segments[i].value;
      const int nx = x + static_cast<int>(acc * static_cast<uint64_t>(w) / sum);
      fillColor(renderer, cx, y, nx - cx, h, segments[i].color);
      if (cx > x) renderer.drawLine(cx, y, cx, y + h - 1, true);
      cx = nx;
    }
  }
  renderer.drawRect(x, y, w, h, true);
}

void drawTile(const GfxRenderer& renderer, const Rect& rect, const char* value, const char* label,
              const char* sublabel) {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 2, 8, true);
  const int pad = 6;
  int valueFont = NOTOSERIF_24_FONT_ID;
  if (renderer.getTextWidth(valueFont, value, EpdFontFamily::BOLD) > rect.width - pad * 2) valueFont = UI_12_FONT_ID;
  const int valueH = renderer.getLineHeight(valueFont);
  const int labelH = renderer.getLineHeight(SMALL_FONT_ID);
  const int lines = (sublabel && sublabel[0]) ? 2 : 1;
  const int y = rect.y + std::max(pad, (rect.height - valueH - labelH * lines) / 2);
  const std::string v = renderer.truncatedText(valueFont, value, rect.width - pad * 2, EpdFontFamily::BOLD);
  const int vw = renderer.getTextWidth(valueFont, v.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(valueFont, rect.x + (rect.width - vw) / 2, y, v.c_str(), true, EpdFontFamily::BOLD);
  const char* texts[2] = {label, sublabel};
  for (int i = 0; i < lines; ++i) {
    const std::string l = renderer.truncatedText(SMALL_FONT_ID, texts[i], rect.width - pad * 2);
    const int lw = renderer.getTextWidth(SMALL_FONT_ID, l.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + (rect.width - lw) / 2, y + valueH + i * labelH, l.c_str());
  }
}

int tileHeight(const GfxRenderer& renderer, const bool sublabel) {
  return renderer.getLineHeight(NOTOSERIF_24_FONT_ID) + renderer.getLineHeight(SMALL_FONT_ID) * (sublabel ? 2 : 1) + 14;
}

void drawArrow(const GfxRenderer& renderer, const int cx, const int cy, const int size, const bool left) {
  const int half = size / 2;
  const int xs[3] = {left ? cx - half : cx + half, left ? cx + half : cx - half, left ? cx + half : cx - half};
  const int ys[3] = {cy, cy - half, cy + half};
  renderer.fillPolygon(xs, ys, 3, true);
}

int drawLegend(const GfxRenderer& renderer, const int x, const int y, const Color color, const char* text) {
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int box = std::max(8, lineH / 2);
  const int by = y + (lineH - box) / 2;
  fillColor(renderer, x, by, box, box, color);
  renderer.drawRect(x, by, box, box, true);
  renderer.drawText(SMALL_FONT_ID, x + box + 5, y, text);
  return x + box + 5 + renderer.getTextWidth(SMALL_FONT_ID, text) + 14;
}

std::string formatDuration(const uint32_t seconds) {
  char buf[16];
  const uint32_t minutes = seconds / 60;
  if (minutes < 60) {
    snprintf(buf, sizeof(buf), "%lu min", static_cast<unsigned long>(minutes));
  } else {
    snprintf(buf, sizeof(buf), "%luh%02lu", static_cast<unsigned long>(minutes / 60),
             static_cast<unsigned long>(minutes % 60));
  }
  return buf;
}

}  // namespace FlashcardChart
