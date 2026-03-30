#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {

namespace PlayerControlButtons {

inline constexpr uint8_t ButtonA = 1u << 0;
inline constexpr uint8_t ButtonB = 1u << 1;
inline constexpr uint8_t ButtonX = 1u << 2;
inline constexpr uint8_t ButtonY = 1u << 3;
inline constexpr uint8_t ButtonSelect = 1u << 4;
inline constexpr uint8_t ButtonStart = 1u << 5;

} // namespace PlayerControlButtons

struct PlayerControlFrame {
    int8_t xAxis = 0;
    int8_t yAxis = 0;
    uint8_t buttons = 0;

    using serialize = zpp::bits::members<3>;
};

inline void to_json(nlohmann::json& j, const PlayerControlFrame& value)
{
    j = nlohmann::json{
        { "xAxis", value.xAxis },
        { "yAxis", value.yAxis },
        { "buttons", value.buttons },
    };
}

inline void from_json(const nlohmann::json& j, PlayerControlFrame& value)
{
    j.at("xAxis").get_to(value.xAxis);
    j.at("yAxis").get_to(value.yAxis);
    j.at("buttons").get_to(value.buttons);
}

} // namespace DirtSim
