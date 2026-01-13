#include "ColorMaterialMapper.h"

#include "ColorNames.h"
#include <cmath>
#include <limits>

namespace DirtSim {

Material::EnumType ColorMaterialMapper::findNearestMaterial(uint8_t r, uint8_t g, uint8_t b)
{
    const auto& materialColors = getMaterialColors();

    float minDistance = std::numeric_limits<float>::max();
    Material::EnumType nearestMaterial = Material::EnumType::Dirt; // Default fallback.

    // Check each material color (skipping AIR at index 0).
    for (size_t i = 1; i <= 9; ++i) {
        const auto& [matR, matG, matB] = materialColors[i];
        const float distance = colorDistance(r, g, b, matR, matG, matB);

        if (distance < minDistance) {
            minDistance = distance;
            nearestMaterial = static_cast<Material::EnumType>(i);
        }
    }

    return nearestMaterial;
}

std::vector<std::vector<Material::EnumType>> ColorMaterialMapper::rgbToMaterials(
    const std::vector<std::vector<RgbPixel>>& rgbPattern, float alphaThreshold)
{
    if (rgbPattern.empty() || rgbPattern[0].empty()) {
        return {};
    }

    const int height = static_cast<int>(rgbPattern.size());
    const int width = static_cast<int>(rgbPattern[0].size());
    const uint8_t alphaThresholdByte = static_cast<uint8_t>(alphaThreshold * 255.0f);

    std::vector<std::vector<Material::EnumType>> result(height);
    for (int y = 0; y < height; ++y) {
        result[y].resize(width);
        for (int x = 0; x < width; ++x) {
            const auto& pixel = rgbPattern[y][x];

            // Transparent pixels map to AIR.
            if (pixel.a < alphaThresholdByte) {
                result[y][x] = Material::EnumType::Air;
            }
            else {
                result[y][x] = findNearestMaterial(pixel.r, pixel.g, pixel.b);
            }
        }
    }

    return result;
}

float ColorMaterialMapper::colorDistance(
    uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2)
{
    const int dr = r2 - r1;
    const int dg = g2 - g1;
    const int db = b2 - b1;
    return std::sqrt(static_cast<float>(dr * dr + dg * dg + db * db));
}

const std::array<std::tuple<uint8_t, uint8_t, uint8_t>, 10>& ColorMaterialMapper::
    getMaterialColors()
{
    using namespace ColorNames;

    // Material colors from ColorNames.cpp, using helper functions to extract RGB.
    // Array index matches Material::EnumType enum value (0=AIR, 1=DIRT, ..., 9=WOOD).
    static const std::array<std::tuple<uint8_t, uint8_t, uint8_t>, 10> colors = { {
        { getR(air()), getG(air()), getB(air()) },
        { getR(dirt()), getG(dirt()), getB(dirt()) },
        { getR(leaf()), getG(leaf()), getB(leaf()) },
        { getR(metal()), getG(metal()), getB(metal()) },
        { getR(root()), getG(root()), getB(root()) },
        { getR(sand()), getG(sand()), getB(sand()) },
        { getR(seed()), getG(seed()), getB(seed()) },
        { getR(stone()), getG(stone()), getB(stone()) },
        { getR(water()), getG(water()), getB(water()) },
        { getR(wood()), getG(wood()), getB(wood()) },
    } };

    return colors;
}

} // namespace DirtSim
