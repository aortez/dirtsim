#pragma once

#include <cstdint>
#include <span>

namespace DirtSim {

enum class WaterActivityFlag : uint8_t {
    HasFluid = 1 << 0,
    Interface = 1 << 1,
};

inline bool hasWaterActivityFlag(uint8_t flags, WaterActivityFlag flag)
{
    return (flags & static_cast<uint8_t>(flag)) != 0;
}

struct WaterActivityView {
    int width = 0;
    int height = 0;
    std::span<const float> max_face_speed;
    std::span<const float> volume_delta;
    std::span<const uint8_t> flags;
};

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
