#include "GlowManager.h"
#include "core/ColorNames.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldLightCalculator.h"

namespace DirtSim {

void GlowManager::apply(
    World& world,
    const std::vector<Vector2i>& digitPositions,
    const std::vector<Vector2i>& floorPositions,
    const std::vector<Vector2i>& obstaclePositions,
    const std::vector<Vector2i>& wallPositions,
    const GlowConfig& config)
{
    WorldLightCalculator& lightCalc = world.getLightCalculator();
    const WorldData& data = world.getData();

    for (const auto& pos : digitPositions) {
        if (data.inBounds(pos.x, pos.y)) {
            lightCalc.setEmissive(pos.x, pos.y, config.digitColor, config.digitIntensity);
        }
    }

    for (const auto& pos : floorPositions) {
        if (data.inBounds(pos.x, pos.y)) {
            lightCalc.setEmissive(pos.x, pos.y, ColorNames::dirt(), config.floorIntensity);
        }
    }

    for (const auto& pos : obstaclePositions) {
        if (data.inBounds(pos.x, pos.y)) {
            lightCalc.setEmissive(
                pos.x, pos.y, ColorNames::torchOrange(), config.obstacleIntensity);
        }
    }

    for (const auto& pos : wallPositions) {
        if (data.inBounds(pos.x, pos.y)) {
            lightCalc.setEmissive(pos.x, pos.y, ColorNames::wood(), config.wallIntensity);
        }
    }

    if (config.waterIntensity > 0.0f) {
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                if (data.at(x, y).material_type == Material::EnumType::Water) {
                    lightCalc.setEmissive(x, y, ColorNames::stormGlow(), config.waterIntensity);
                }
            }
        }
    }
}

} // namespace DirtSim
