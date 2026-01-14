#pragma once

#include "core/Vector2.h"

namespace DirtSim {

/**
 * Per-frame input that the brain sends to the duck.
 *
 * The brain decides what to do (high-level action) and produces
 * a low-level input each frame. The duck processes the input.
 *
 * Movement and jump are independent axes - the brain can request
 * both simultaneously (e.g., jump while running right).
 */
struct DuckInput {
    Vector2f move{};   // Movement force: x [-1,1] left/right, y [-1,1] up/down.
    bool jump = false; // Request jump this frame.
};

} // namespace DirtSim
