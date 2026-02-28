#pragma once

#include "core/organisms/DuckSensoryData.h"
#include "core/organisms/evolution/NesPolicyLayout.h"
#include "core/scenarios/nes/NesPaletteFrame.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace DirtSim {

struct NesGameAdapterControllerInput {
    uint8_t inferredControllerMask = 0;
    std::optional<uint8_t> lastGameState = std::nullopt;
};

struct NesGameAdapterFrameInput {
    uint64_t advancedFrames = 0;
    uint8_t controllerMask = 0;
    const NesPaletteFrame* paletteFrame = nullptr;
    std::optional<SmolnesRuntime::MemorySnapshot> memorySnapshot = std::nullopt;
};

struct NesGameAdapterFrameOutput {
    bool done = false;
    double rewardDelta = 0.0;
    std::optional<uint8_t> gameState = std::nullopt;
    std::optional<std::array<float, NesPolicyLayout::InputCount>> features = std::nullopt;
};

struct NesGameAdapterSensoryInput {
    std::array<float, NesPolicyLayout::InputCount> policyInputs{};
    uint8_t controllerMask = 0;
    const NesPaletteFrame* paletteFrame = nullptr;
    std::optional<uint8_t> lastGameState = std::nullopt;
    double deltaTimeSeconds = 0.0;
};

/**
 * NES game-specific control policy and frame evaluation hooks.
 */
class NesGameAdapter {
public:
    virtual ~NesGameAdapter() = default;

    virtual void reset(const std::string& runtimeRomId) { (void)runtimeRomId; }
    virtual uint8_t resolveControllerMask(const NesGameAdapterControllerInput& input) = 0;
    virtual NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) = 0;
    virtual DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const = 0;
};

std::unique_ptr<NesGameAdapter> createNesFlappyParatroopaGameAdapter();
std::unique_ptr<NesGameAdapter> createNesSuperTiltBroGameAdapter();

} // namespace DirtSim
