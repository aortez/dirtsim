#pragma once

#include "core/MaterialType.h"
#include "core/Vector2.h"

namespace DirtSim {

/**
 * A cell in an organism's local coordinate system.
 *
 * Local cells define the organism's shape relative to its anchor position.
 * These are projected onto the grid each frame based on the organism's
 * continuous position.
 */
struct LocalCell {
    Vector2i localPos;
    Material::EnumType material;
    double fillRatio;
};

} // namespace DirtSim
