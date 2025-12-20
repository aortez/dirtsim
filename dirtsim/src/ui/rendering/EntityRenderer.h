#pragma once

#include "core/Entity.h"
#include <cstdint>
#include <vector>

namespace DirtSim {
namespace Ui {

/**
 * @brief Renders entities (duck, sparkle, etc.) on top of the world canvas.
 *
 * Entities are sprite-based overlays that render after the cell grid.
 */
void renderEntities(
    const std::vector<Entity>& entities,
    uint32_t* pixels,
    uint32_t canvasWidth,
    uint32_t canvasHeight,
    uint32_t scaledCellWidth,
    uint32_t scaledCellHeight);

} // namespace Ui
} // namespace DirtSim
