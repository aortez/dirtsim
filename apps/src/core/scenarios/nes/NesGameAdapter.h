#pragma once

#include "core/organisms/DuckSensoryData.h"
#include "core/organisms/evolution/NesPolicyLayout.h"
#include "core/scenarios/nes/NesControllerTelemetry.h"
#include "core/scenarios/nes/NesFitnessDetails.h"
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

struct NesGameAdapterControllerOutput {
    uint8_t resolvedControllerMask = 0;
    NesGameAdapterControllerSource source = NesGameAdapterControllerSource::InferredPolicy;
    std::optional<uint64_t> sourceFrameIndex = std::nullopt;
};

struct NesGameAdapterFrameInput {
    uint64_t advancedFrames = 0;
    uint8_t controllerMask = 0;
    const NesPaletteFrame* paletteFrame = nullptr;
    std::optional<SmolnesRuntime::MemorySnapshot> memorySnapshot = std::nullopt;
};

struct NesGameAdapterDebugState {
    std::optional<uint64_t> advancedFrameCount = std::nullopt;
    std::optional<uint8_t> level = std::nullopt;
    std::optional<uint8_t> lifeState = std::nullopt;
    std::optional<uint8_t> lives = std::nullopt;
    std::optional<uint8_t> phase = std::nullopt;
    std::optional<uint8_t> playerXScreen = std::nullopt;
    std::optional<uint8_t> playerYScreen = std::nullopt;
    std::optional<uint8_t> powerupState = std::nullopt;
    std::optional<uint8_t> world = std::nullopt;
    std::optional<uint16_t> absoluteX = std::nullopt;
    std::optional<bool> setupFailure = std::nullopt;
    std::optional<bool> setupScriptActive = std::nullopt;
};

struct NesGameAdapterFrameOutput {
    bool done = false;
    std::optional<NesGameAdapterDebugState> debugState = std::nullopt;
    NesFitnessDetails fitnessDetails{};
    double rewardDelta = 0.0;
    std::optional<uint8_t> gameState = std::nullopt;
};

struct NesGameAdapterSensoryInput {
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
    virtual NesGameAdapterControllerOutput resolveControllerMask(
        const NesGameAdapterControllerInput& input) = 0;
    virtual NesGameAdapterFrameOutput evaluateFrame(const NesGameAdapterFrameInput& input) = 0;
    virtual DuckSensoryData makeDuckSensoryData(const NesGameAdapterSensoryInput& input) const = 0;
};

std::unique_ptr<NesGameAdapter> createNesFlappyParatroopaGameAdapter();
std::unique_ptr<NesGameAdapter> createNesSuperMarioBrosGameAdapter();
std::unique_ptr<NesGameAdapter> createNesSuperTiltBroGameAdapter();

} // namespace DirtSim
