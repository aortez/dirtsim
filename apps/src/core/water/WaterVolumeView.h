#pragma once

#include <span>

namespace DirtSim {

struct WaterVolumeView {
    int width = 0;
    int height = 0;
    std::span<const float> volume;
};

struct WaterVolumeMutableView {
    int width = 0;
    int height = 0;
    std::span<float> volume;
};

} // namespace DirtSim
