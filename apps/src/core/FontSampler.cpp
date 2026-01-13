#include "FontSampler.h"

#include <algorithm>
#include <array>
#include <lvgl.h>
#include <spdlog/spdlog.h>

#if LV_USE_FREETYPE
#include "lvgl/src/libs/freetype/lv_freetype.h"
#endif

namespace DirtSim {

namespace {

// Track whether FreeType has been initialized.
bool g_freetypeInitialized = false;

} // namespace

namespace {

lv_display_t* ensureHeadlessDisplay()
{
    static bool initialized = false;
    static lv_display_t* headlessDisplay = nullptr;

    if (!initialized) {
        initialized = true;

        // Check if LVGL is already initialized with a display.
        if (lv_display_get_default()) {
            return nullptr; // Use existing display.
        }

        // Initialize LVGL if needed.
        if (!lv_is_initialized()) {
            lv_init();
        }

        // Create a minimal headless display.
        headlessDisplay = lv_display_create(100, 100);
        if (headlessDisplay) {
            spdlog::info("FontSampler: Created headless LVGL display for font rendering");
        }
    }

    return headlessDisplay;
}

} // namespace

FontSampler::FontSampler(const lv_font_t* font, int targetWidth, int targetHeight, float threshold)
    : font_(font), targetWidth_(targetWidth), targetHeight_(targetHeight), threshold_(threshold)
{
    initCanvas();
}

FontSampler::FontSampler(
    const std::string& fontPath, int fontSize, int targetWidth, int targetHeight, float threshold)
    : font_(nullptr),
      targetWidth_(targetWidth),
      targetHeight_(targetHeight),
      threshold_(threshold),
      ownsFont_(true)
{
#if LV_USE_FREETYPE
    initFreeType();

    // Create FreeType font from file. Use BITMAP mode for color emoji support.
    ownedFont_ = lv_freetype_font_create(
        fontPath.c_str(),
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
        static_cast<uint32_t>(fontSize),
        LV_FREETYPE_FONT_STYLE_NORMAL);

    if (ownedFont_) {
        font_ = ownedFont_;
        spdlog::info("FontSampler: Loaded FreeType font from {} (size {})", fontPath, fontSize);
    }
    else {
        spdlog::error("FontSampler: Failed to load font from {}", fontPath);
        ownsFont_ = false;
    }
#else
    spdlog::error("FontSampler: Cannot load font from file - FreeType support not enabled "
                  "(LV_USE_FREETYPE=0)");
    (void)fontPath;
    (void)fontSize;
#endif

    initCanvas();
}

FontSampler::~FontSampler()
{
    destroyCanvas();

#if LV_USE_FREETYPE
    if (ownsFont_ && ownedFont_) {
        lv_freetype_font_delete(ownedFont_);
        ownedFont_ = nullptr;
        ownsFont_ = false;
    }
#endif
}

FontSampler::FontSampler(FontSampler&& other) noexcept
    : font_(other.font_),
      targetWidth_(other.targetWidth_),
      targetHeight_(other.targetHeight_),
      threshold_(other.threshold_),
      ownsFont_(other.ownsFont_),
      ownedFont_(other.ownedFont_),
      canvas_(other.canvas_),
      drawBuf_(other.drawBuf_),
      cache_(std::move(other.cache_))
{
    other.canvas_ = nullptr;
    other.drawBuf_ = nullptr;
    other.ownsFont_ = false;
    other.ownedFont_ = nullptr;
}

FontSampler& FontSampler::operator=(FontSampler&& other) noexcept
{
    if (this != &other) {
        destroyCanvas();

#if LV_USE_FREETYPE
        // Clean up our owned font before taking over other's.
        if (ownsFont_ && ownedFont_) {
            lv_freetype_font_delete(ownedFont_);
        }
#endif

        font_ = other.font_;
        targetWidth_ = other.targetWidth_;
        targetHeight_ = other.targetHeight_;
        threshold_ = other.threshold_;
        ownsFont_ = other.ownsFont_;
        ownedFont_ = other.ownedFont_;
        canvas_ = other.canvas_;
        drawBuf_ = other.drawBuf_;
        cache_ = std::move(other.cache_);
        other.canvas_ = nullptr;
        other.drawBuf_ = nullptr;
        other.ownsFont_ = false;
        other.ownedFont_ = nullptr;
    }
    return *this;
}

void FontSampler::initCanvas()
{
    // Ensure we have a display (creates headless one if needed).
    ensureHeadlessDisplay();

    auto* buf =
        lv_draw_buf_create(targetWidth_, targetHeight_, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);

    if (!buf) {
        spdlog::error("FontSampler: Failed to create draw buffer");
        return;
    }

    drawBuf_ = buf;

    auto* canvas = lv_canvas_create(lv_screen_active());
    if (!canvas) {
        spdlog::error("FontSampler: Failed to create canvas");
        lv_draw_buf_destroy(buf);
        drawBuf_ = nullptr;
        return;
    }

    lv_canvas_set_draw_buf(canvas, buf);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);

    canvas_ = canvas;
}

void FontSampler::destroyCanvas()
{
    if (canvas_) {
        lv_obj_delete(static_cast<lv_obj_t*>(canvas_));
        canvas_ = nullptr;
    }

    if (drawBuf_) {
        lv_draw_buf_destroy(static_cast<lv_draw_buf_t*>(drawBuf_));
        drawBuf_ = nullptr;
    }
}

void FontSampler::initFreeType()
{
#if LV_USE_FREETYPE
    if (!g_freetypeInitialized) {
        // Initialize FreeType with glyph cache. Use the configured cache size.
        lv_result_t result = lv_freetype_init(LV_FREETYPE_CACHE_FT_GLYPH_CNT);
        if (result == LV_RESULT_OK) {
            g_freetypeInitialized = true;
            spdlog::info("FontSampler: FreeType initialized successfully");
        }
        else {
            spdlog::error("FontSampler: Failed to initialize FreeType");
        }
    }
#endif
}

std::vector<std::vector<bool>> FontSampler::sampleCharacter(char c)
{
    return sampleCharacter(c, threshold_);
}

std::vector<std::vector<bool>> FontSampler::sampleCharacter(char c, float threshold)
{
    if (!canvas_ || !drawBuf_) {
        spdlog::warn("FontSampler: Canvas not initialized");
        return {};
    }

    auto* canvas = static_cast<lv_obj_t*>(canvas_);

    // Clear canvas to black.
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    // Initialize layer for drawing.
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    // Set up label descriptor.
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = lv_color_white();
    dsc.font = font_;
    dsc.opa = LV_OPA_COVER;

    // Create single-character string.
    char text[2] = { c, '\0' };
    dsc.text = text;

    // Draw with margin so we can detect clipping.
    // If filled pixels appear at canvas edge (outside margin), glyph is clipped.
    constexpr int MARGIN = 2;
    lv_area_t coords = { MARGIN, MARGIN, targetWidth_ - 1 - MARGIN, targetHeight_ - 1 - MARGIN };
    lv_draw_label(&layer, &dsc, &coords);

    // Finalize drawing.
    lv_canvas_finish_layer(canvas, &layer);

    return sampleCurrentCanvas(threshold);
}

std::vector<std::vector<bool>> FontSampler::sampleUtf8Character(const std::string& utf8Char)
{
    if (!canvas_ || !drawBuf_) {
        spdlog::warn("FontSampler: Canvas not initialized");
        return {};
    }

    auto* canvas = static_cast<lv_obj_t*>(canvas_);

    // Clear canvas to black.
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    // Initialize layer for drawing.
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    // Set up label descriptor.
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = lv_color_white();
    dsc.font = font_;
    dsc.opa = LV_OPA_COVER;
    dsc.text = utf8Char.c_str();

    // Draw with margin so we can detect clipping.
    constexpr int MARGIN = 2;
    lv_area_t coords = { MARGIN, MARGIN, targetWidth_ - 1 - MARGIN, targetHeight_ - 1 - MARGIN };
    lv_draw_label(&layer, &dsc, &coords);

    // Finalize drawing.
    lv_canvas_finish_layer(canvas, &layer);

    return sampleCurrentCanvas(threshold_);
}

std::vector<std::vector<bool>> FontSampler::sampleCurrentCanvas(float threshold)
{
    auto* canvas = static_cast<lv_obj_t*>(canvas_);

    // Create result grid.
    std::vector<std::vector<bool>> result(targetHeight_);
    for (int y = 0; y < targetHeight_; ++y) {
        result[y].resize(targetWidth_, false);
    }

    // Brightness threshold (0-255).
    const uint8_t brightnessThreshold = static_cast<uint8_t>(threshold * 255.0f);

    // Sample each pixel.
    for (int y = 0; y < targetHeight_; ++y) {
        for (int x = 0; x < targetWidth_; ++x) {
            lv_color32_t px = lv_canvas_get_px(canvas, x, y);

            // Calculate brightness as average of RGB.
            const uint8_t brightness = (px.red + px.green + px.blue) / 3;

            result[y][x] = (brightness > brightnessThreshold);
        }
    }

    return result;
}

std::vector<std::vector<RgbPixel>> FontSampler::sampleCharacterRgb(char c)
{
    if (!canvas_ || !drawBuf_) {
        spdlog::warn("FontSampler: Canvas not initialized");
        return {};
    }

    auto* canvas = static_cast<lv_obj_t*>(canvas_);

    // Clear canvas to transparent so unfilled areas map to AIR.
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

    // Initialize layer for drawing.
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    // Set up label descriptor.
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = lv_color_white();
    dsc.font = font_;
    dsc.opa = LV_OPA_COVER;

    // Create single-character string.
    char text[2] = { c, '\0' };
    dsc.text = text;

    // Draw with margin.
    constexpr int MARGIN = 2;
    lv_area_t coords = { MARGIN, MARGIN, targetWidth_ - 1 - MARGIN, targetHeight_ - 1 - MARGIN };
    lv_draw_label(&layer, &dsc, &coords);

    // Finalize drawing.
    lv_canvas_finish_layer(canvas, &layer);

    return sampleCurrentCanvasRgb();
}

std::vector<std::vector<RgbPixel>> FontSampler::sampleUtf8CharacterRgb(const std::string& utf8Char)
{
    if (!canvas_ || !drawBuf_) {
        spdlog::warn("FontSampler: Canvas not initialized");
        return {};
    }

    auto* canvas = static_cast<lv_obj_t*>(canvas_);

    // Clear canvas to transparent so unfilled areas map to AIR.
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

    // Initialize layer for drawing.
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    // Set up label descriptor.
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = lv_color_white();
    dsc.font = font_;
    dsc.opa = LV_OPA_COVER;
    dsc.text = utf8Char.c_str();

    // Draw with margin.
    constexpr int MARGIN = 2;
    lv_area_t coords = { MARGIN, MARGIN, targetWidth_ - 1 - MARGIN, targetHeight_ - 1 - MARGIN };
    lv_draw_label(&layer, &dsc, &coords);

    // Finalize drawing.
    lv_canvas_finish_layer(canvas, &layer);

    return sampleCurrentCanvasRgb();
}

std::vector<std::vector<Material::EnumType>> FontSampler::sampleCharacterMaterial(
    char c, float alphaThreshold)
{
    auto rgbPattern = sampleCharacterRgb(c);
    return ColorMaterialMapper::rgbToMaterials(rgbPattern, alphaThreshold);
}

std::vector<std::vector<Material::EnumType>> FontSampler::sampleUtf8CharacterMaterial(
    const std::string& utf8Char, float alphaThreshold)
{
    auto rgbPattern = sampleUtf8CharacterRgb(utf8Char);
    return ColorMaterialMapper::rgbToMaterials(rgbPattern, alphaThreshold);
}

std::vector<std::vector<RgbPixel>> FontSampler::sampleCurrentCanvasRgb()
{
    auto* canvas = static_cast<lv_obj_t*>(canvas_);

    // Create result grid.
    std::vector<std::vector<RgbPixel>> result(targetHeight_);
    for (int y = 0; y < targetHeight_; ++y) {
        result[y].resize(targetWidth_);
    }

    // Sample each pixel.
    for (int y = 0; y < targetHeight_; ++y) {
        for (int x = 0; x < targetWidth_; ++x) {
            lv_color32_t px = lv_canvas_get_px(canvas, x, y);
            result[y][x] = { px.red, px.green, px.blue, px.alpha };
        }
    }

    return result;
}

GridBuffer<RgbPixel> FontSampler::sampleCurrentCanvasRgbGrid()
{
    auto* canvas = static_cast<lv_obj_t*>(canvas_);

    GridBuffer<RgbPixel> result;
    result.resize(targetWidth_, targetHeight_);

    // Sample each pixel into flat buffer.
    for (int y = 0; y < targetHeight_; ++y) {
        RgbPixel* row = result.row(y);
        for (int x = 0; x < targetWidth_; ++x) {
            lv_color32_t px = lv_canvas_get_px(canvas, x, y);
            row[x] = { px.red, px.green, px.blue, px.alpha };
        }
    }

    return result;
}

GridBuffer<RgbPixel> FontSampler::sampleUtf8CharacterRgbGrid(const std::string& utf8Char)
{
    if (!canvas_ || !drawBuf_) {
        spdlog::warn("FontSampler: Canvas not initialized");
        return {};
    }

    auto* canvas = static_cast<lv_obj_t*>(canvas_);

    // Clear canvas to transparent so unfilled areas map to AIR.
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);

    // Initialize layer for drawing.
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    // Set up label descriptor.
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = lv_color_white();
    dsc.font = font_;
    dsc.opa = LV_OPA_COVER;
    dsc.text = utf8Char.c_str();

    // Draw with margin.
    constexpr int MARGIN = 2;
    lv_area_t coords = { MARGIN, MARGIN, targetWidth_ - 1 - MARGIN, targetHeight_ - 1 - MARGIN };
    lv_draw_label(&layer, &dsc, &coords);

    // Finalize drawing.
    lv_canvas_finish_layer(canvas, &layer);

    return sampleCurrentCanvasRgbGrid();
}

GridBuffer<Material::EnumType> FontSampler::sampleUtf8CharacterMaterialGrid(
    const std::string& utf8Char, float alphaThreshold)
{
    GridBuffer<RgbPixel> rgb = sampleUtf8CharacterRgbGrid(utf8Char);

    GridBuffer<Material::EnumType> result;
    result.resize(rgb.width, rgb.height, Material::EnumType::AIR);

    const uint8_t alphaThresholdByte = static_cast<uint8_t>(alphaThreshold * 255.0f);

    for (int y = 0; y < rgb.height; ++y) {
        const RgbPixel* srcRow = rgb.row(y);
        Material::EnumType* dstRow = result.row(y);
        for (int x = 0; x < rgb.width; ++x) {
            const RgbPixel& px = srcRow[x];
            if (px.a >= alphaThresholdByte) {
                dstRow[x] = ColorMaterialMapper::findNearestMaterial(px.r, px.g, px.b);
            }
            // else: already AIR from resize default.
        }
    }

    return result;
}

const std::vector<std::vector<bool>>& FontSampler::getCachedPattern(char c)
{
    auto it = cache_.find(c);
    if (it != cache_.end()) {
        return it->second;
    }

    // Sample and cache.
    cache_[c] = sampleCharacter(c);
    return cache_[c];
}

void FontSampler::clearCache()
{
    cache_.clear();
    trimmedCache_.clear();
}

void FontSampler::precacheAscii()
{
    // Cache printable ASCII (space through tilde).
    for (char c = ' '; c <= '~'; ++c) {
        getCachedPattern(c);
    }
}

void FontSampler::resizeCanvas(int newWidth, int newHeight)
{
    if (newWidth == targetWidth_ && newHeight == targetHeight_) {
        return;
    }

    destroyCanvas();
    targetWidth_ = newWidth;
    targetHeight_ = newHeight;
    initCanvas();

    // Clear caches since canvas size changed.
    cache_.clear();
    trimmedCache_.clear();
}

std::vector<std::vector<bool>> FontSampler::sampleCharacterTrimmed(char c)
{
    return sampleCharacterTrimmed(c, threshold_);
}

std::vector<std::vector<bool>> FontSampler::sampleCharacterTrimmed(char c, float threshold)
{
    constexpr int MAX_RESIZE_ATTEMPTS = 3;
    int attempts = 0;

    while (attempts < MAX_RESIZE_ATTEMPTS) {
        auto pattern = sampleCharacter(c, threshold);

        if (pattern.empty()) {
            return {};
        }

        if (hasClipping(pattern)) {
            int newWidth = targetWidth_ * 2;
            int newHeight = targetHeight_ * 2;
            spdlog::warn(
                "FontSampler: Clipping detected for '{}' at {}x{}, resizing to {}x{}",
                c,
                targetWidth_,
                targetHeight_,
                newWidth,
                newHeight);
            resizeCanvas(newWidth, newHeight);
            attempts++;
        }
        else {
            return trimPattern(pattern);
        }
    }

    spdlog::error(
        "FontSampler: Still clipping after {} resize attempts for '{}', returning trimmed anyway",
        MAX_RESIZE_ATTEMPTS,
        c);
    return trimPattern(sampleCharacter(c, threshold));
}

const std::vector<std::vector<bool>>& FontSampler::getCachedPatternTrimmed(char c)
{
    auto it = trimmedCache_.find(c);
    if (it != trimmedCache_.end()) {
        return it->second;
    }

    // Sample, trim, and cache.
    trimmedCache_[c] = sampleCharacterTrimmed(c);
    return trimmedCache_[c];
}

bool FontSampler::hasClipping(const std::vector<std::vector<bool>>& pattern)
{
    if (pattern.empty() || pattern[0].empty()) {
        return false;
    }

    int height = static_cast<int>(pattern.size());
    int width = static_cast<int>(pattern[0].size());

    // Check top row.
    for (int x = 0; x < width; ++x) {
        if (pattern[0][x]) {
            return true;
        }
    }

    // Check bottom row.
    for (int x = 0; x < width; ++x) {
        if (pattern[height - 1][x]) {
            return true;
        }
    }

    // Check left column.
    for (int y = 0; y < height; ++y) {
        if (pattern[y][0]) {
            return true;
        }
    }

    // Check right column.
    for (int y = 0; y < height; ++y) {
        if (pattern[y][width - 1]) {
            return true;
        }
    }

    return false;
}

std::vector<std::vector<bool>> FontSampler::trimPattern(
    const std::vector<std::vector<bool>>& pattern)
{
    if (pattern.empty() || pattern[0].empty()) {
        return {};
    }

    int height = static_cast<int>(pattern.size());
    int width = static_cast<int>(pattern[0].size());

    // Find bounding box of filled pixels.
    int minX = width;
    int maxX = -1;
    int minY = height;
    int maxY = -1;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (pattern[y][x]) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }

    // No filled pixels found.
    if (maxX < 0) {
        return {};
    }

    // Extract trimmed region.
    int trimmedWidth = maxX - minX + 1;
    int trimmedHeight = maxY - minY + 1;

    std::vector<std::vector<bool>> result(trimmedHeight);
    for (int y = 0; y < trimmedHeight; ++y) {
        result[y].resize(trimmedWidth);
        for (int x = 0; x < trimmedWidth; ++x) {
            result[y][x] = pattern[minY + y][minX + x];
        }
    }

    return result;
}

GridBuffer<Material::EnumType> FontSampler::sampleAndDownsample(
    const std::string& utf8Char, int targetWidth, int targetHeight, float alphaThreshold)
{
    // Sample at native font resolution.
    auto fullGrid = sampleUtf8CharacterMaterialGrid(utf8Char, alphaThreshold);

    if (fullGrid.width == 0 || fullGrid.height == 0) {
        return {};
    }

    // If already at target size or smaller, return as-is.
    if (fullGrid.width <= targetWidth && fullGrid.height <= targetHeight) {
        return fullGrid;
    }

    // Downsample to target size.
    return downsample(fullGrid, targetWidth, targetHeight);
}

GridBuffer<Material::EnumType> FontSampler::downsample(
    const GridBuffer<Material::EnumType>& src, int targetWidth, int targetHeight)
{
    if (src.width == 0 || src.height == 0 || targetWidth <= 0 || targetHeight <= 0) {
        return {};
    }

    GridBuffer<Material::EnumType> result;
    result.resize(targetWidth, targetHeight, Material::EnumType::AIR);

    const int srcW = src.width;
    const int srcH = src.height;

    // Calculate scaling factors.
    const float scaleX = static_cast<float>(srcW) / targetWidth;
    const float scaleY = static_cast<float>(srcH) / targetHeight;

    // For each target pixel, find the most common material in the source region.
    for (int ty = 0; ty < targetHeight; ++ty) {
        for (int tx = 0; tx < targetWidth; ++tx) {
            // Source region bounds.
            const int srcX0 = static_cast<int>(tx * scaleX);
            const int srcY0 = static_cast<int>(ty * scaleY);
            const int srcX1 = std::min(static_cast<int>((tx + 1) * scaleX), srcW);
            const int srcY1 = std::min(static_cast<int>((ty + 1) * scaleY), srcH);

            // Count materials in region (excluding AIR for voting).
            std::array<int, 10> counts = {};
            int nonAirTotal = 0;

            for (int sy = srcY0; sy < srcY1; ++sy) {
                const Material::EnumType* row = src.row(sy);
                for (int sx = srcX0; sx < srcX1; ++sx) {
                    Material::EnumType mat = row[sx];
                    counts[static_cast<size_t>(mat)]++;
                    if (mat != Material::EnumType::AIR) {
                        nonAirTotal++;
                    }
                }
            }

            // If mostly AIR, keep AIR. Otherwise pick most common non-AIR material.
            int regionSize = (srcX1 - srcX0) * (srcY1 - srcY0);
            if (nonAirTotal * 2 < regionSize) {
                // Less than half is non-AIR, keep as AIR.
                result.at(tx, ty) = Material::EnumType::AIR;
            }
            else {
                // Find most common non-AIR material.
                Material::EnumType best = Material::EnumType::AIR;
                int bestCount = 0;
                for (size_t i = 1; i < counts.size(); ++i) {
                    if (counts[i] > bestCount) {
                        bestCount = counts[i];
                        best = static_cast<Material::EnumType>(i);
                    }
                }
                result.at(tx, ty) = best;
            }
        }
    }

    return result;
}

GridBuffer<RgbPixel> FontSampler::downsample(
    const GridBuffer<RgbPixel>& src, int targetWidth, int targetHeight)
{
    if (src.width == 0 || src.height == 0 || targetWidth <= 0 || targetHeight <= 0) {
        return {};
    }

    GridBuffer<RgbPixel> result;
    result.resize(targetWidth, targetHeight);

    const int srcW = src.width;
    const int srcH = src.height;

    // Calculate scaling factors.
    const float scaleX = static_cast<float>(srcW) / targetWidth;
    const float scaleY = static_cast<float>(srcH) / targetHeight;

    // For each target pixel, compute alpha-weighted average of source region.
    for (int ty = 0; ty < targetHeight; ++ty) {
        for (int tx = 0; tx < targetWidth; ++tx) {
            // Source region bounds.
            const int srcX0 = static_cast<int>(tx * scaleX);
            const int srcY0 = static_cast<int>(ty * scaleY);
            const int srcX1 = std::min(static_cast<int>((tx + 1) * scaleX), srcW);
            const int srcY1 = std::min(static_cast<int>((ty + 1) * scaleY), srcH);

            // Alpha-weighted sum.
            float sumR = 0, sumG = 0, sumB = 0, sumA = 0;

            for (int sy = srcY0; sy < srcY1; ++sy) {
                const RgbPixel* row = src.row(sy);
                for (int sx = srcX0; sx < srcX1; ++sx) {
                    const RgbPixel& px = row[sx];
                    float a = px.a / 255.0f;
                    sumR += px.r * a;
                    sumG += px.g * a;
                    sumB += px.b * a;
                    sumA += a;
                }
            }

            // Compute weighted average.
            RgbPixel& out = result.at(tx, ty);
            if (sumA > 0.001f) {
                out.r = static_cast<uint8_t>(std::min(255.0f, sumR / sumA));
                out.g = static_cast<uint8_t>(std::min(255.0f, sumG / sumA));
                out.b = static_cast<uint8_t>(std::min(255.0f, sumB / sumA));
                // Output alpha is average alpha over region.
                int regionSize = (srcX1 - srcX0) * (srcY1 - srcY0);
                out.a = static_cast<uint8_t>(std::min(255.0f, (sumA / regionSize) * 255.0f));
            }
            else {
                out = { 0, 0, 0, 0 };
            }
        }
    }

    return result;
}

} // namespace DirtSim
