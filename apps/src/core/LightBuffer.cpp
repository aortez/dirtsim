#include "LightBuffer.h"

#include <algorithm>

namespace DirtSim {

void LightBuffer::resize(uint32_t w, uint32_t h)
{
    width = w;
    height = h;
    data.resize(static_cast<size_t>(w) * h, 0xFFFFFFFF);
}

void LightBuffer::clear(uint32_t value)
{
    std::fill(data.begin(), data.end(), value);
}

uint32_t LightBuffer::at(uint32_t x, uint32_t y) const
{
    return data[y * width + x];
}

void LightBuffer::set(uint32_t x, uint32_t y, uint32_t value)
{
    data[y * width + x] = value;
}

} // namespace DirtSim
