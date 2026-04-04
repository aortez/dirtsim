#include "core/scenarios/nes/NesDuckSensoryBuilder.h"

#include "core/organisms/evolution/NesPolicyLayout.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace DirtSim {

namespace {

void setPreviousControlFromControllerMask(DuckSensoryData& sensory, uint8_t controllerMask)
{
    const bool leftHeld = (controllerMask & NesPolicyLayout::ButtonLeft) != 0u;
    const bool rightHeld = (controllerMask & NesPolicyLayout::ButtonRight) != 0u;
    if (leftHeld != rightHeld) {
        sensory.previous_control_x = leftHeld ? -1.0f : 1.0f;
    }

    const bool upHeld = (controllerMask & NesPolicyLayout::ButtonUp) != 0u;
    const bool downHeld = (controllerMask & NesPolicyLayout::ButtonDown) != 0u;
    if (upHeld != downHeld) {
        sensory.previous_control_y = upHeld ? -1.0f : 1.0f;
    }

    sensory.previous_jump = (controllerMask & NesPolicyLayout::ButtonA) != 0u;
    sensory.previous_run = (controllerMask & NesPolicyLayout::ButtonB) != 0u;
}

} // namespace

DuckSensoryData makeNesDuckSensoryDataFromPaletteFrame(
    const NesPaletteClusterer& clusterer, const NesPaletteFrame& frame, double deltaTimeSeconds)
{
    DuckSensoryData sensory{};
    sensory.actual_width = DuckSensoryData::GRID_SIZE;
    sensory.actual_height = DuckSensoryData::GRID_SIZE;
    sensory.scale_factor = 1.0;
    sensory.world_offset = { 0, 0 };
    sensory.position = { DuckSensoryData::GRID_SIZE / 2, DuckSensoryData::GRID_SIZE / 2 };
    sensory.delta_time_seconds = deltaTimeSeconds;

    if (frame.width == 0 || frame.height == 0) {
        return sensory;
    }

    const size_t expectedSize =
        static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
    if (frame.indices.size() < expectedSize) {
        return sensory;
    }

    for (auto& row : sensory.material_histograms) {
        for (auto& cell : row) {
            cell.fill(0.0f);
        }
    }

    constexpr int gridSize = DuckSensoryData::GRID_SIZE;
    constexpr int channelCount = DuckSensoryData::NUM_MATERIALS;

    for (int gy = 0; gy < gridSize; ++gy) {
        const uint32_t y0 = (static_cast<uint32_t>(gy) * frame.height) / gridSize;
        const uint32_t y1 = (static_cast<uint32_t>(gy + 1) * frame.height) / gridSize;

        for (int gx = 0; gx < gridSize; ++gx) {
            const uint32_t x0 = (static_cast<uint32_t>(gx) * frame.width) / gridSize;
            const uint32_t x1 = (static_cast<uint32_t>(gx + 1) * frame.width) / gridSize;

            std::array<uint32_t, channelCount> counts{};
            counts.fill(0);

            uint32_t totalPixels = 0;
            for (uint32_t y = y0; y < y1; ++y) {
                const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(frame.width);
                for (uint32_t x = x0; x < x1; ++x) {
                    const size_t idx = rowBase + static_cast<size_t>(x);
                    const uint8_t paletteIndex = frame.indices[idx] & 0x3F;
                    const uint8_t clusterIndex = clusterer.mapIndex(paletteIndex);
                    if (clusterIndex < channelCount) {
                        counts[static_cast<size_t>(clusterIndex)] += 1u;
                    }
                    totalPixels += 1u;
                }
            }

            if (totalPixels == 0) {
                continue;
            }

            const float denom = static_cast<float>(totalPixels);
            auto& histogram =
                sensory.material_histograms[static_cast<size_t>(gy)][static_cast<size_t>(gx)];
            for (int c = 0; c < channelCount; ++c) {
                histogram[static_cast<size_t>(c)] =
                    static_cast<float>(counts[static_cast<size_t>(c)]) / denom;
            }
        }
    }

    return sensory;
}

DuckSensoryData makeNesDuckSensoryData(
    const NesPaletteClusterer& clusterer,
    const NesPaletteFrame* frame,
    double deltaTimeSeconds,
    const std::array<double, DuckSensoryData::SPECIAL_SENSE_COUNT>& specialSenses,
    float facingX,
    uint8_t controllerMask)
{
    DuckSensoryData sensory{};
    sensory.delta_time_seconds = deltaTimeSeconds;
    if (frame != nullptr) {
        sensory = makeNesDuckSensoryDataFromPaletteFrame(clusterer, *frame, deltaTimeSeconds);
    }

    sensory.facing_x = facingX;
    setPreviousControlFromControllerMask(sensory, controllerMask);
    sensory.special_senses = specialSenses;
    return sensory;
}

} // namespace DirtSim
