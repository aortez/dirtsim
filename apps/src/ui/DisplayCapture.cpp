#include "DisplayCapture.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <lvgl/lvgl.h>
#include <spdlog/spdlog.h>

// C API for lodepng.
extern "C" {
unsigned lodepng_encode32(
    unsigned char** out, size_t* outsize, const unsigned char* image, unsigned w, unsigned h);
const char* lodepng_error_text(unsigned code);
}

namespace DirtSim {
namespace Ui {

namespace {

void alphaBlendArgb8888(
    std::vector<uint8_t>& base,
    uint32_t baseWidth,
    uint32_t baseHeight,
    const uint8_t* overlay,
    uint32_t overlayWidth,
    uint32_t overlayHeight)
{
    const uint32_t width = std::min(baseWidth, overlayWidth);
    const uint32_t height = std::min(baseHeight, overlayHeight);

    for (uint32_t y = 0; y < height; y++) {
        const size_t baseRow = static_cast<size_t>(y) * baseWidth * 4;
        const size_t overlayRow = static_cast<size_t>(y) * overlayWidth * 4;
        for (uint32_t x = 0; x < width; x++) {
            const size_t baseIdx = baseRow + static_cast<size_t>(x) * 4;
            const size_t overlayIdx = overlayRow + static_cast<size_t>(x) * 4;
            const uint8_t alpha = overlay[overlayIdx + 3];
            if (alpha == 0) {
                continue;
            }
            if (alpha == 255) {
                base[baseIdx + 0] = overlay[overlayIdx + 0];
                base[baseIdx + 1] = overlay[overlayIdx + 1];
                base[baseIdx + 2] = overlay[overlayIdx + 2];
                base[baseIdx + 3] = 255;
                continue;
            }

            const uint16_t inv = static_cast<uint16_t>(255 - alpha);
            base[baseIdx + 0] = static_cast<uint8_t>(
                (overlay[overlayIdx + 0] * alpha + base[baseIdx + 0] * inv + 127) / 255);
            base[baseIdx + 1] = static_cast<uint8_t>(
                (overlay[overlayIdx + 1] * alpha + base[baseIdx + 1] * inv + 127) / 255);
            base[baseIdx + 2] = static_cast<uint8_t>(
                (overlay[overlayIdx + 2] * alpha + base[baseIdx + 2] * inv + 127) / 255);
            base[baseIdx + 3] = 255;
        }
    }
}

void blendLayer(
    std::vector<uint8_t>& base, uint32_t baseWidth, uint32_t baseHeight, lv_obj_t* layer)
{
    if (!layer || lv_obj_get_child_cnt(layer) == 0) {
        return;
    }

    lv_draw_buf_t* layerBuf = lv_snapshot_take(layer, LV_COLOR_FORMAT_ARGB8888);
    if (!layerBuf) {
        spdlog::debug("DisplayCapture: lv_snapshot_take failed for layer");
        return;
    }

    alphaBlendArgb8888(
        base,
        baseWidth,
        baseHeight,
        static_cast<const uint8_t*>(layerBuf->data),
        layerBuf->header.w,
        layerBuf->header.h);
    lv_draw_buf_destroy(layerBuf);
}

} // namespace

std::optional<ScreenshotData> captureDisplayPixels(lv_display_t* display, double scale)
{
    if (!display) {
        spdlog::error("DisplayCapture: Display is null");
        return std::nullopt;
    }

    // Clamp scale to reasonable range.
    scale = std::max(0.1, std::min(1.0, scale));

    // Get display dimensions.
    uint32_t fullWidth = lv_display_get_horizontal_resolution(display);
    uint32_t fullHeight = lv_display_get_vertical_resolution(display);

    if (fullWidth == 0 || fullHeight == 0) {
        spdlog::error("DisplayCapture: Display has zero dimensions");
        return std::nullopt;
    }

    // Get the screen (root object of the display).
    lv_obj_t* screen = lv_display_get_screen_active(display);
    if (!screen) {
        spdlog::error("DisplayCapture: No active screen on display");
        return std::nullopt;
    }

    // Take snapshot using LVGL's snapshot API.
    lv_draw_buf_t* screenBuf = lv_snapshot_take(screen, LV_COLOR_FORMAT_ARGB8888);
    if (!screenBuf) {
        spdlog::error("DisplayCapture: lv_snapshot_take failed");
        return std::nullopt;
    }

    // Get buffer info.
    uint32_t bufWidth = screenBuf->header.w;
    uint32_t bufHeight = screenBuf->header.h;
    const uint8_t* bufData = static_cast<const uint8_t*>(screenBuf->data);

    std::vector<uint8_t> compositePixels(static_cast<size_t>(bufWidth) * bufHeight * 4);
    std::memcpy(compositePixels.data(), bufData, compositePixels.size());
    lv_draw_buf_destroy(screenBuf);

    blendLayer(compositePixels, bufWidth, bufHeight, lv_display_get_layer_top(display));
    blendLayer(compositePixels, bufWidth, bufHeight, lv_display_get_layer_sys(display));

    // Calculate scaled dimensions.
    uint32_t scaledWidth = static_cast<uint32_t>(bufWidth * scale);
    uint32_t scaledHeight = static_cast<uint32_t>(bufHeight * scale);

    ScreenshotData data;
    if (scale >= 0.99) {
        data.width = bufWidth;
        data.height = bufHeight;
        data.pixels = std::move(compositePixels);
    }
    else {
        // Simple nearest-neighbor downsampling if scale < 1.0.
        data.width = scaledWidth;
        data.height = scaledHeight;
        data.pixels.resize(static_cast<size_t>(scaledWidth) * scaledHeight * 4);

        for (uint32_t y = 0; y < scaledHeight; y++) {
            for (uint32_t x = 0; x < scaledWidth; x++) {
                uint32_t srcX = static_cast<uint32_t>(x / scale);
                uint32_t srcY = static_cast<uint32_t>(y / scale);
                size_t srcIdx = (static_cast<size_t>(srcY) * bufWidth + srcX) * 4;
                size_t dstIdx = (static_cast<size_t>(y) * scaledWidth + x) * 4;
                data.pixels[dstIdx + 0] = compositePixels[srcIdx + 0];
                data.pixels[dstIdx + 1] = compositePixels[srcIdx + 1];
                data.pixels[dstIdx + 2] = compositePixels[srcIdx + 2];
                data.pixels[dstIdx + 3] = compositePixels[srcIdx + 3];
            }
        }
    }

    spdlog::debug(
        "DisplayCapture: Captured {}x{} -> {}x{} (scale={:.2f}, {} bytes)",
        bufWidth,
        bufHeight,
        scaledWidth,
        scaledHeight,
        scale,
        data.pixels.size());
    return data;
}

std::vector<uint8_t> encodePNG(const uint8_t* pixels, uint32_t width, uint32_t height)
{
    // Convert ARGB8888 to RGBA8888 for lodepng.
    size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgbaPixels(pixelCount * 4);

    for (size_t i = 0; i < pixelCount * 4; i += 4) {
        // LVGL ARGB8888 is little-endian: B G R A in memory.
        // lodepng wants RGBA: R G B A.
        rgbaPixels[i + 0] = pixels[i + 2]; // R (from B position)
        rgbaPixels[i + 1] = pixels[i + 1]; // G (same position)
        rgbaPixels[i + 2] = pixels[i + 0]; // B (from R position)
        rgbaPixels[i + 3] = pixels[i + 3]; // A (same position)
    }

    unsigned char* pngData = nullptr;
    size_t pngSize = 0;
    unsigned error = lodepng_encode32(&pngData, &pngSize, rgbaPixels.data(), width, height);

    if (error) {
        spdlog::error(
            "DisplayCapture: PNG encoding failed: {} ({})", lodepng_error_text(error), error);
        return {};
    }

    // Copy to vector and free lodepng memory.
    std::vector<uint8_t> result(pngData, pngData + pngSize);
    free(pngData);

    spdlog::info("DisplayCapture: Encoded PNG ({} bytes)", pngSize);
    return result;
}

std::string base64Encode(const std::vector<uint8_t>& data)
{
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string encoded;
    encoded.reserve((data.size() + 2) / 3 * 4);

    size_t i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];

    for (size_t idx = 0; idx < data.size(); idx++) {
        char_array_3[i++] = data[idx];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                encoded += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (size_t j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (size_t j = 0; j < i + 1; j++)
            encoded += base64_chars[char_array_4[j]];

        while (i++ < 3)
            encoded += '=';
    }

    return encoded;
}

} // namespace Ui
} // namespace DirtSim
