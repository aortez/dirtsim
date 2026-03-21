#pragma once

#include <cstdint>
#include <optional>

namespace DirtSim {

struct NesSuperMarioBrosSetupDecision {
    uint8_t controllerMask = 0;
    bool gameplayDetected = false;
    bool pressedStart = false;
    bool usingSetupScript = false;
    uint64_t frameIndex = 0;
};

uint64_t getNesSuperMarioBrosSetupFailureFrame();
uint64_t getNesSuperMarioBrosSetupScriptEndFrame();
uint8_t getNesSuperMarioBrosScriptedSetupMaskForFrame(uint64_t frameIndex);
NesSuperMarioBrosSetupDecision resolveNesSuperMarioBrosSetupDecision(
    uint64_t frameIndex, std::optional<uint8_t> lastGameState, uint8_t inferredControllerMask);

} // namespace DirtSim
