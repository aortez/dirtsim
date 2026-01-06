#include "FontSampler.h"

#include <lvgl.h>
#include <spdlog/spdlog.h>

namespace DirtSim {

FontSampler::FontSampler(
    const lv_font_t* font,
    int targetWidth,
    int targetHeight,
    float threshold)
    : font_(font)
    , targetWidth_(targetWidth)
    , targetHeight_(targetHeight)
    , threshold_(threshold)
{
    initCanvas();
}

FontSampler::~FontSampler()
{
    destroyCanvas();
}

FontSampler::FontSampler(FontSampler&& other) noexcept
    : font_(other.font_)
    , targetWidth_(other.targetWidth_)
    , targetHeight_(other.targetHeight_)
    , threshold_(other.threshold_)
    , canvas_(other.canvas_)
    , drawBuf_(other.drawBuf_)
    , cache_(std::move(other.cache_))
{
    other.canvas_ = nullptr;
    other.drawBuf_ = nullptr;
}

FontSampler& FontSampler::operator=(FontSampler&& other) noexcept
{
    if (this != &other) {
        destroyCanvas();
        font_ = other.font_;
        targetWidth_ = other.targetWidth_;
        targetHeight_ = other.targetHeight_;
        threshold_ = other.threshold_;
        canvas_ = other.canvas_;
        drawBuf_ = other.drawBuf_;
        cache_ = std::move(other.cache_);
        other.canvas_ = nullptr;
        other.drawBuf_ = nullptr;
    }
    return *this;
}

void FontSampler::initCanvas()
{
    // Create draw buffer with ARGB8888 format for easy pixel reading.
    // Use lv_draw_buf_create which properly allocates and initializes.
    auto* buf = lv_draw_buf_create(
        targetWidth_,
        targetHeight_,
        LV_COLOR_FORMAT_ARGB8888,
        LV_STRIDE_AUTO);

    if (!buf) {
        spdlog::error("FontSampler: Failed to create draw buffer");
        return;
    }

    drawBuf_ = buf;

    // Get the default display (required for canvas creation).
    // If no display exists, canvas creation will fail.
    lv_display_t* display = lv_display_get_default();
    if (!display) {
        spdlog::debug("FontSampler: No display, canvas creation may fail");
    }

    // Create canvas on the active screen (requires a display).
    auto* canvas = lv_canvas_create(lv_screen_active());
    if (!canvas) {
        spdlog::error("FontSampler: Failed to create canvas");
        lv_draw_buf_destroy(buf);
        drawBuf_ = nullptr;
        return;
    }

    lv_canvas_set_draw_buf(canvas, buf);

    // Hide the canvas since we only use it for offscreen rendering.
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
    char text[2] = {c, '\0'};
    dsc.text = text;

    // Draw with margin so we can detect clipping.
    // If filled pixels appear at canvas edge (outside margin), glyph is clipped.
    constexpr int MARGIN = 2;
    lv_area_t coords = {MARGIN, MARGIN, targetWidth_ - 1 - MARGIN, targetHeight_ - 1 - MARGIN};
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
    lv_area_t coords = {MARGIN, MARGIN, targetWidth_ - 1 - MARGIN, targetHeight_ - 1 - MARGIN};
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

std::vector<std::vector<bool>> FontSampler::trimPattern(const std::vector<std::vector<bool>>& pattern)
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

} // namespace DirtSim
