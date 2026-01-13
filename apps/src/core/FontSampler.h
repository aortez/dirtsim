#pragma once

#include "ColorMaterialMapper.h"
#include "GridBuffer.h"
#include "MaterialType.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration to avoid LVGL in header.
struct _lv_font_t;
typedef struct _lv_font_t lv_font_t;

namespace DirtSim {

/**
 * Samples characters from LVGL fonts into boolean or RGB grid patterns.
 *
 * Uses LVGL's canvas and text rendering to convert any supported font
 * character into 2D patterns suitable for cell-based rendering. Supports
 * boolean thresholding, full RGB sampling, and automatic dithering to
 * Material::EnumTypes for colored emoji rendering.
 *
 * Supports two font sources:
 * 1. Built-in LVGL fonts (passed as const lv_font_t*)
 * 2. Runtime-loaded fonts via FreeType (TTF files, including color emoji)
 */
class FontSampler {
public:
    // Constructor for built-in LVGL fonts.
    FontSampler(const lv_font_t* font, int targetWidth, int targetHeight, float threshold = 0.3f);

    // Constructor for runtime-loaded fonts (TTF files via FreeType).
    // Supports color emoji fonts like NotoColorEmoji.ttf.
    FontSampler(
        const std::string& fontPath,
        int fontSize,
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

    std::vector<std::vector<RgbPixel>> sampleCharacterRgb(char c);
    std::vector<std::vector<RgbPixel>> sampleUtf8CharacterRgb(const std::string& utf8Char);

    // GridBuffer-based sampling for cache-friendly access.
    GridBuffer<RgbPixel> sampleUtf8CharacterRgbGrid(const std::string& utf8Char);
    GridBuffer<Material::EnumType> sampleUtf8CharacterMaterialGrid(
        const std::string& utf8Char, float alphaThreshold = 0.5f);

    std::vector<std::vector<Material::EnumType>> sampleCharacterMaterial(
        char c, float alphaThreshold = 0.5f);
    std::vector<std::vector<Material::EnumType>> sampleUtf8CharacterMaterial(
        const std::string& utf8Char, float alphaThreshold = 0.5f);

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
    static std::vector<std::vector<bool>> trimPattern(
        const std::vector<std::vector<bool>>& pattern);
    static bool hasClipping(const std::vector<std::vector<bool>>& pattern);

    // Downsampling utilities. Reduces grid resolution for display.
    // For materials: uses majority voting (most common material in source region wins).
    // For RGB: uses alpha-weighted averaging.
    static GridBuffer<Material::EnumType> downsample(
        const GridBuffer<Material::EnumType>& src, int targetWidth, int targetHeight);
    static GridBuffer<RgbPixel> downsample(
        const GridBuffer<RgbPixel>& src, int targetWidth, int targetHeight);

    // Combined sample + downsample for convenience.
    // Samples at native font resolution, then downsamples to target size.
    GridBuffer<Material::EnumType> sampleAndDownsample(
        const std::string& utf8Char,
        int targetWidth,
        int targetHeight,
        float alphaThreshold = 0.5f);

    int getWidth() const { return targetWidth_; }
    int getHeight() const { return targetHeight_; }
    float getThreshold() const { return threshold_; }
    void setThreshold(float threshold) { threshold_ = threshold; }

private:
    void initCanvas();
    void destroyCanvas();
    void initFreeType();
    std::vector<std::vector<bool>> sampleCurrentCanvas(float threshold);
    std::vector<std::vector<RgbPixel>> sampleCurrentCanvasRgb();
    GridBuffer<RgbPixel> sampleCurrentCanvasRgbGrid();

    const lv_font_t* font_;
    int targetWidth_;
    int targetHeight_;
    float threshold_;

    // FreeType font ownership. If true, we created the font and must delete it.
    bool ownsFont_ = false;
    lv_font_t* ownedFont_ = nullptr; // Non-const pointer for fonts we own.

    // LVGL canvas resources (stored as void* to avoid LVGL in header).
    void* canvas_ = nullptr;
    void* drawBuf_ = nullptr;

    std::unordered_map<char, std::vector<std::vector<bool>>> cache_;
    std::unordered_map<char, std::vector<std::vector<bool>>> trimmedCache_;
};

} // namespace DirtSim
