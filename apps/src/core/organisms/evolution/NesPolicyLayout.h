#pragma once

#include <cstddef>
#include <cstdint>

namespace DirtSim::NesPolicyLayout {

inline constexpr const char* FlappyParatroopaWorldUnlRomId = "flappy-paratroopa-world-unl";

inline constexpr int InputCount = 12;
inline constexpr int OutputCount = 8;
inline constexpr size_t WeightCount =
    (static_cast<size_t>(InputCount) * static_cast<size_t>(OutputCount))
    + static_cast<size_t>(OutputCount);

inline constexpr uint8_t ButtonA = (1u << 0);
inline constexpr uint8_t ButtonB = (1u << 1);
inline constexpr uint8_t ButtonSelect = (1u << 2);
inline constexpr uint8_t ButtonStart = (1u << 3);
inline constexpr uint8_t ButtonUp = (1u << 4);
inline constexpr uint8_t ButtonDown = (1u << 5);
inline constexpr uint8_t ButtonLeft = (1u << 6);
inline constexpr uint8_t ButtonRight = (1u << 7);

} // namespace DirtSim::NesPolicyLayout
