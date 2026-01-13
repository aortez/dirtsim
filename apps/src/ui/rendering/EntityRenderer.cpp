#include "EntityRenderer.h"
#include "ui/controls/duck_img.h"
#include "ui/controls/goose_img.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

// Extract light multipliers from RGBA color (ColorNames format: R<<24|G<<16|B<<8|A).
// Returns normalized values [0.0, 1.0] for each channel.
static void extractLightMultipliers(uint32_t light_color, float& r, float& g, float& b)
{
    r = static_cast<float>((light_color >> 24) & 0xFF) / 255.0f;
    g = static_cast<float>((light_color >> 16) & 0xFF) / 255.0f;
    b = static_cast<float>((light_color >> 8) & 0xFF) / 255.0f;
}

void renderEntities(
    const std::vector<Entity>& entities,
    uint32_t* pixels,
    uint32_t canvasWidth,
    uint32_t canvasHeight,
    uint32_t scaledCellWidth,
    uint32_t scaledCellHeight)
{
    if (entities.empty()) return;

    static bool logged_once = false;
    if (!logged_once) {
        spdlog::info(
            "EntityRenderer: Rendering {} entities (cell size: {}x{} pixels)",
            entities.size(),
            scaledCellWidth,
            scaledCellHeight);
        logged_once = true;
    }

    for (const auto& entity : entities) {
        if (!entity.visible) continue;

        // Calculate entity position in pixels.
        // Entity position is in cell coordinates, COM provides sub-cell precision.
        float entityX = entity.position.x + (entity.com.x + 1.0f) * 0.5f;
        float entityY = entity.position.y + (entity.com.y + 1.0f) * 0.5f;

        // Convert to pixel coordinates.
        int32_t pixelX = static_cast<int32_t>(entityX * scaledCellWidth);
        int32_t pixelY = static_cast<int32_t>(entityY * scaledCellHeight);

        if (entity.type == EntityType::Duck) {
            // Draw attached sparkles first (behind duck).
            for (const auto& sparkle : entity.sparkles) {
                uint8_t alpha = static_cast<uint8_t>(sparkle.opacity * 255);
                if (alpha == 0) continue;

                // Convert sparkle position to pixels.
                int32_t sparklePixelX = static_cast<int32_t>(sparkle.position.x * scaledCellWidth);
                int32_t sparklePixelY = static_cast<int32_t>(sparkle.position.y * scaledCellHeight);

                // Draw sparkle as a small cross (5 pixels).
                const int32_t offsets[5][2] = {
                    { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
                };
                for (const auto& offset : offsets) {
                    int32_t px = sparklePixelX + offset[0];
                    int32_t py = sparklePixelY + offset[1];

                    if (px >= 0 && px < static_cast<int32_t>(canvasWidth) && py >= 0
                        && py < static_cast<int32_t>(canvasHeight)) {
                        uint32_t idx = py * canvasWidth + px;

                        // Alpha blend sparkle (yellow).
                        uint32_t dstColor = pixels[idx];
                        uint8_t invAlpha = 255 - alpha;

                        uint8_t srcR = 0xFF, srcG = 0xFF, srcB = 0x00;
                        uint8_t dstR = (dstColor >> 16) & 0xFF;
                        uint8_t dstG = (dstColor >> 8) & 0xFF;
                        uint8_t dstB = dstColor & 0xFF;

                        uint8_t outR = (srcR * alpha + dstR * invAlpha) / 255;
                        uint8_t outG = (srcG * alpha + dstG * invAlpha) / 255;
                        uint8_t outB = (srcB * alpha + dstB * invAlpha) / 255;

                        pixels[idx] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
                    }
                }
            }

            // Duck is 1 cell in size.
            int32_t duckPixelWidth = static_cast<int32_t>(scaledCellWidth);
            int32_t duckPixelHeight = static_cast<int32_t>(scaledCellHeight);

            // Scale factor from source image to cell size.
            float scaleX = static_cast<float>(duckPixelWidth) / DUCK_IMG_WIDTH;
            float scaleY = static_cast<float>(duckPixelHeight) / DUCK_IMG_HEIGHT;

            // Center duck on its position, shifted up a bit to align feet with floor.
            int32_t duckStartX = pixelX - duckPixelWidth / 2;
            int32_t duckStartY =
                pixelY - duckPixelHeight / 2 - static_cast<int32_t>(duckPixelHeight * 0.45);

            // Draw duck sprite with scaling.
            // Flip horizontally if facing vector points left.
            const uint8_t* duckData = duck_img_data;

            // Extract light multipliers for this entity.
            float lightR, lightG, lightB;
            extractLightMultipliers(entity.light_color, lightR, lightG, lightB);

            // Apply emission boost - duck glows warm yellow-orange based on sparkle intensity.
            if (entity.emission > 0.0f) {
                lightR = std::min(1.0f, lightR + entity.emission * 1.0f);
                lightG = std::min(1.0f, lightG + entity.emission * 0.8f);
                lightB = std::min(1.0f, lightB + entity.emission * 0.4f);
            }

            for (int32_t dy = 0; dy < duckPixelHeight; dy++) {
                // Source Y in original duck image.
                int32_t srcY = static_cast<int32_t>(dy / scaleY);
                if (srcY >= DUCK_IMG_HEIGHT) srcY = DUCK_IMG_HEIGHT - 1;

                int32_t destY = duckStartY + dy;
                if (destY < 0 || destY >= static_cast<int32_t>(canvasHeight)) continue;

                for (int32_t dx = 0; dx < duckPixelWidth; dx++) {
                    // Source X in original duck image.
                    int32_t srcX = static_cast<int32_t>(dx / scaleX);
                    if (srcX >= DUCK_IMG_WIDTH) srcX = DUCK_IMG_WIDTH - 1;

                    // Flip horizontally if facing right (duck sprite faces left by default).
                    if (entity.facing.x > 0) {
                        srcX = DUCK_IMG_WIDTH - 1 - srcX;
                    }

                    int32_t destX = duckStartX + dx;
                    if (destX < 0 || destX >= static_cast<int32_t>(canvasWidth)) continue;

                    // Get source pixel (ARGB8888 format: B, G, R, A order in memory).
                    size_t srcIdx = (srcY * DUCK_IMG_WIDTH + srcX) * 4;
                    uint8_t b = duckData[srcIdx + 0];
                    uint8_t g = duckData[srcIdx + 1];
                    uint8_t r = duckData[srcIdx + 2];
                    uint8_t a = duckData[srcIdx + 3];

                    // Skip fully transparent pixels.
                    if (a == 0) continue;

                    // Apply lighting to sprite colors.
                    r = static_cast<uint8_t>(r * lightR);
                    g = static_cast<uint8_t>(g * lightG);
                    b = static_cast<uint8_t>(b * lightB);

                    uint32_t destIdx = destY * canvasWidth + destX;

                    if (a == 255) {
                        // Fully opaque - direct write.
                        pixels[destIdx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                    else {
                        // Alpha blend.
                        uint32_t dstColor = pixels[destIdx];
                        uint8_t invAlpha = 255 - a;

                        uint8_t dstR = (dstColor >> 16) & 0xFF;
                        uint8_t dstG = (dstColor >> 8) & 0xFF;
                        uint8_t dstB = dstColor & 0xFF;

                        uint8_t outR = (r * a + dstR * invAlpha) / 255;
                        uint8_t outG = (g * a + dstG * invAlpha) / 255;
                        uint8_t outB = (b * a + dstB * invAlpha) / 255;

                        pixels[destIdx] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
                    }
                }
            }
        }
        else if (entity.type == EntityType::Goose) {
            // Goose is 1 cell in size.
            int32_t goosePixelWidth = static_cast<int32_t>(scaledCellWidth);
            int32_t goosePixelHeight = static_cast<int32_t>(scaledCellHeight);

            // Scale factor from source image to cell size.
            float scaleX = static_cast<float>(goosePixelWidth) / GOOSE_IMG_WIDTH;
            float scaleY = static_cast<float>(goosePixelHeight) / GOOSE_IMG_HEIGHT;

            // Center goose on its position, shifted up 0.5 cells.
            int32_t gooseStartX = pixelX - goosePixelWidth / 2;
            int32_t gooseStartY = pixelY - goosePixelHeight / 2; // - goosePixelHeight / 2;

            // Draw goose sprite with scaling.
            const uint8_t* gooseData = goose_img_data;

            // Extract light multipliers for this entity.
            float lightR, lightG, lightB;
            extractLightMultipliers(entity.light_color, lightR, lightG, lightB);

            for (int32_t dy = 0; dy < goosePixelHeight; dy++) {
                int32_t srcY = static_cast<int32_t>(dy / scaleY);
                if (srcY >= GOOSE_IMG_HEIGHT) srcY = GOOSE_IMG_HEIGHT - 1;

                int32_t destY = gooseStartY + dy;
                if (destY < 0 || destY >= static_cast<int32_t>(canvasHeight)) continue;

                for (int32_t dx = 0; dx < goosePixelWidth; dx++) {
                    int32_t srcX = static_cast<int32_t>(dx / scaleX);
                    if (srcX >= GOOSE_IMG_WIDTH) srcX = GOOSE_IMG_WIDTH - 1;

                    // Flip horizontally if facing right (goose sprite faces left by default).
                    if (entity.facing.x > 0) {
                        srcX = GOOSE_IMG_WIDTH - 1 - srcX;
                    }

                    int32_t destX = gooseStartX + dx;
                    if (destX < 0 || destX >= static_cast<int32_t>(canvasWidth)) continue;

                    // Get source pixel (ARGB8888 format: B, G, R, A order in memory).
                    size_t srcIdx = (srcY * GOOSE_IMG_WIDTH + srcX) * 4;
                    uint8_t b = gooseData[srcIdx + 0];
                    uint8_t g = gooseData[srcIdx + 1];
                    uint8_t r = gooseData[srcIdx + 2];
                    uint8_t a = gooseData[srcIdx + 3];

                    if (a == 0) continue;

                    // Apply lighting to sprite colors.
                    r = static_cast<uint8_t>(r * lightR);
                    g = static_cast<uint8_t>(g * lightG);
                    b = static_cast<uint8_t>(b * lightB);

                    uint32_t destIdx = destY * canvasWidth + destX;

                    if (a == 255) {
                        pixels[destIdx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                    else {
                        uint32_t dstColor = pixels[destIdx];
                        uint8_t invAlpha = 255 - a;

                        uint8_t dstR = (dstColor >> 16) & 0xFF;
                        uint8_t dstG = (dstColor >> 8) & 0xFF;
                        uint8_t dstB = dstColor & 0xFF;

                        uint8_t outR = (r * a + dstR * invAlpha) / 255;
                        uint8_t outG = (g * a + dstG * invAlpha) / 255;
                        uint8_t outB = (b * a + dstB * invAlpha) / 255;

                        pixels[destIdx] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
                    }
                }
            }
        }
        else if (entity.type == EntityType::Sparkle) {
            // Legacy standalone sparkle (for backwards compatibility).
            float opacity = std::min(entity.velocity.magnitude(), 1.0f);
            uint8_t alpha = static_cast<uint8_t>(opacity * 255);

            if (alpha == 0) continue;

            // Draw sparkle as a small cross (5 pixels).
            const int32_t offsets[5][2] = { { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
            for (const auto& offset : offsets) {
                int32_t px = pixelX + offset[0];
                int32_t py = pixelY + offset[1];

                if (px >= 0 && px < static_cast<int32_t>(canvasWidth) && py >= 0
                    && py < static_cast<int32_t>(canvasHeight)) {
                    uint32_t idx = py * canvasWidth + px;

                    // Alpha blend sparkle (yellow).
                    uint32_t dstColor = pixels[idx];
                    uint8_t invAlpha = 255 - alpha;

                    uint8_t srcR = 0xFF, srcG = 0xFF, srcB = 0x00;
                    uint8_t dstR = (dstColor >> 16) & 0xFF;
                    uint8_t dstG = (dstColor >> 8) & 0xFF;
                    uint8_t dstB = dstColor & 0xFF;

                    uint8_t outR = (srcR * alpha + dstR * invAlpha) / 255;
                    uint8_t outG = (srcG * alpha + dstG * invAlpha) / 255;
                    uint8_t outB = (srcB * alpha + dstB * invAlpha) / 255;

                    pixels[idx] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
                }
            }
        }
    }
}

} // namespace Ui
} // namespace DirtSim
