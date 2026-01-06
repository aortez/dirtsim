#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration to avoid LVGL in header.
struct _lv_font_t;
typedef struct _lv_font_t lv_font_t;

namespace DirtSim {

/**
 * Samples characters from LVGL fonts into boolean grid patterns.
 *
 * Uses LVGL's canvas and text rendering to convert any supported font
 * character into a 2D boolean grid suitable for cell-based rendering.
 *
 * TODO: Add full RGB color sampling support:
 *   1. sampleCharacterRgb() - Return full RGBA per pixel (already available
 *      from lv_canvas_get_px, just need to expose it).
 *   2. Add color-to-material mapping that either:
 *      - Thresholds to binary on/off (current behavior), or
 *      - Dithers into the closest primary MaterialType color per cell.
 *   This would enable colored emoji and multi-material text rendering.
 */
class FontSampler {
public:
    FontSampler(
        const lv_font_t* font,
        int targetWidth,
        int targetHeight,
        float threshold = 0.3f);

    ~FontSampler();

    // Non-copyable due to LVGL resource ownership.
    FontSampler(const FontSampler&) = delete;
    FontSampler& operator=(const FontSampler&) = delete;

    // Movable.
    FontSampler(FontSampler&& other) noexcept;
    FontSampler& operator=(FontSampler&& other) noexcept;

    std::vector<std::vector<bool>> sampleCharacter(char c);
    std::vector<std::vector<bool>> sampleCharacter(char c, float threshold);
    std::vector<std::vector<bool>> sampleUtf8Character(const std::string& utf8Char);

    // Samples character, auto-resizes canvas if clipping detected, and trims whitespace.
    // Returns a tight-fitting pattern with the font's natural aspect ratio.
    std::vector<std::vector<bool>> sampleCharacterTrimmed(char c);
    std::vector<std::vector<bool>> sampleCharacterTrimmed(char c, float threshold);

    const std::vector<std::vector<bool>>& getCachedPattern(char c);
    const std::vector<std::vector<bool>>& getCachedPatternTrimmed(char c);
    void clearCache();
    void precacheAscii();

    // Resize the internal canvas. Clears the cache since patterns may change.
    void resizeCanvas(int newWidth, int newHeight);

    // Pattern utility functions.
    static std::vector<std::vector<bool>> trimPattern(const std::vector<std::vector<bool>>& pattern);
    static bool hasClipping(const std::vector<std::vector<bool>>& pattern);

    int getWidth() const { return targetWidth_; }
    int getHeight() const { return targetHeight_; }
    float getThreshold() const { return threshold_; }
    void setThreshold(float threshold) { threshold_ = threshold; }

private:
    void initCanvas();
    void destroyCanvas();
    std::vector<std::vector<bool>> sampleCurrentCanvas(float threshold);

    const lv_font_t* font_;
    int targetWidth_;
    int targetHeight_;
    float threshold_;

    // LVGL canvas resources (stored as void* to avoid LVGL in header).
    void* canvas_ = nullptr;
    void* drawBuf_ = nullptr;

    std::unordered_map<char, std::vector<std::vector<bool>>> cache_;
    std::unordered_map<char, std::vector<std::vector<bool>>> trimmedCache_;
};

} // namespace DirtSim
