#pragma once

namespace DirtSim {

/**
 * Movement direction for the duck.
 */
enum class DuckMovement {
    NONE,  // No movement input (stand still).
    LEFT,  // Walk left.
    RIGHT  // Walk right.
};

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
    DuckMovement movement = DuckMovement::NONE;  // Horizontal movement direction.
    bool jump = false;                            // Request jump this frame.
};

} // namespace DirtSim
