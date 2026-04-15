#include "core/scenarios/nes/NesTileSensoryData.h"

#include "core/organisms/evolution/NesPolicyLayout.h"

#include <cstdint>

namespace DirtSim {

void setNesTilePreviousControlFromControllerMask(
    NesTileSensoryData& sensory, uint8_t controllerMask)
{
    const bool leftHeld = (controllerMask & NesPolicyLayout::ButtonLeft) != 0u;
    const bool rightHeld = (controllerMask & NesPolicyLayout::ButtonRight) != 0u;
    sensory.previousControlX = 0.0f;
    if (leftHeld != rightHeld) {
        sensory.previousControlX = leftHeld ? -1.0f : 1.0f;
    }

    const bool upHeld = (controllerMask & NesPolicyLayout::ButtonUp) != 0u;
    const bool downHeld = (controllerMask & NesPolicyLayout::ButtonDown) != 0u;
    sensory.previousControlY = 0.0f;
    if (upHeld != downHeld) {
        sensory.previousControlY = upHeld ? -1.0f : 1.0f;
    }

    sensory.previousA = (controllerMask & NesPolicyLayout::ButtonA) != 0u;
    sensory.previousB = (controllerMask & NesPolicyLayout::ButtonB) != 0u;
}

} // namespace DirtSim
