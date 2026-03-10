#include "core/scenarios/nes/NesSuperMarioBrosSetupPolicy.h"

#include "core/organisms/evolution/NesPolicyLayout.h"

namespace DirtSim {

namespace {

constexpr uint64_t kSetupFailureFrame = 500u;
constexpr uint64_t kSetupScriptEndFrame = 300u;
constexpr uint64_t kStartPressFirstFrame = 120u;

} // namespace

uint64_t getNesSuperMarioBrosSetupFailureFrame()
{
    return kSetupFailureFrame;
}

uint64_t getNesSuperMarioBrosSetupScriptEndFrame()
{
    return kSetupScriptEndFrame;
}

uint8_t getNesSuperMarioBrosScriptedSetupMaskForFrame(uint64_t frameIndex)
{
    if (frameIndex == kStartPressFirstFrame) {
        return NesPolicyLayout::ButtonStart;
    }

    return 0u;
}

NesSuperMarioBrosSetupDecision resolveNesSuperMarioBrosSetupDecision(
    uint64_t frameIndex, std::optional<uint8_t> lastGameState, uint8_t inferredControllerMask)
{
    NesSuperMarioBrosSetupDecision decision;
    decision.frameIndex = frameIndex;
    decision.gameplayDetected = lastGameState.value_or(0u) == 1u;

    if (!decision.gameplayDetected) {
        decision.controllerMask = getNesSuperMarioBrosScriptedSetupMaskForFrame(frameIndex);
        decision.pressedStart = decision.controllerMask == NesPolicyLayout::ButtonStart;
        decision.usingSetupScript = true;
        return decision;
    }

    decision.controllerMask = inferredControllerMask;
    return decision;
}

} // namespace DirtSim
