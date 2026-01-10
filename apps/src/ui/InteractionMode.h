#pragma once

#include <string>

namespace DirtSim {
namespace Ui {

enum class InteractionMode { NONE, DRAW, ERASE };

inline std::string interactionModeToString(InteractionMode mode)
{
    switch (mode) {
        case InteractionMode::NONE:
            return "None";
        case InteractionMode::DRAW:
            return "Draw";
        case InteractionMode::ERASE:
            return "Erase";
        default:
            return "Unknown";
    }
}

} // namespace Ui
} // namespace DirtSim
