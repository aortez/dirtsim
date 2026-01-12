#pragma once

#include "GridBuffer.h"

namespace DirtSim {

/**
 * Buffer storing raw light values before material color multiplication.
 * Used for entity lighting so sprites aren't tinted by cell material.
 *
 * This is a GridBuffer<uint32_t> with light-specific default values.
 */
struct LightBuffer : GridBuffer<uint32_t> {
    // Resize with white default (0xFFFFFFFF) for unlit areas.
    void resize(int w, int h) { GridBuffer<uint32_t>::resize(w, h, 0xFFFFFFFF); }

    // Clear with black opaque default (0x000000FF).
    void clear(uint32_t value = 0x000000FF) { GridBuffer<uint32_t>::clear(value); }
};

} // namespace DirtSim
