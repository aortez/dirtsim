#pragma once

#include "MaterialType.h"
#include <array>
#include <cstdint>
#include <tuple>
#include <vector>

namespace DirtSim {

// RGB pixel with alpha channel.
struct RgbPixel {
    uint8_t r, g, b, a;
};

/**
 * Maps RGB colors to Material::EnumType via Euclidean distance matching.
 *
 * Enables automatic dithering of colored images/emojis into cell-based
 * material patterns by finding the closest Material::EnumType for each pixel color.
 */
class ColorMaterialMapper {
public:
    static Material::EnumType findNearestMaterial(uint8_t r, uint8_t g, uint8_t b);

    static std::vector<std::vector<Material::EnumType>> rgbToMaterials(
        const std::vector<std::vector<RgbPixel>>& rgbPattern, float alphaThreshold = 0.5f);

private:
    static float colorDistance(
        uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2);

    static const std::array<std::tuple<uint8_t, uint8_t, uint8_t>, 10>& getMaterialColors();
};

} // namespace DirtSim
