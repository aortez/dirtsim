#pragma once

#include <cstdint>
#include <vector>

namespace DirtSim {

// Buffer storing raw light values before material color multiplication.
// Used for entity lighting so sprites aren't tinted by cell material.
struct LightBuffer {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> data;

    void resize(uint32_t w, uint32_t h);
    void clear(uint32_t value = 0x000000FF);
    uint32_t at(uint32_t x, uint32_t y) const;
    void set(uint32_t x, uint32_t y, uint32_t value);
};

} // namespace DirtSim
