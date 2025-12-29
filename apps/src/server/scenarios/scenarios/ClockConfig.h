#pragma once

#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim::Config {

enum class ClockFont : uint8_t {
    DotMatrix = 0,
    Segment7 = 1,
    Segment7Large = 2,
    Segment7Tall = 3,
};

NLOHMANN_JSON_SERIALIZE_ENUM(
    ClockFont,
    {
        {ClockFont::DotMatrix, "DotMatrix"},
        {ClockFont::Segment7, "Segment7"},
        {ClockFont::Segment7Large, "Segment7Large"},
        {ClockFont::Segment7Tall, "Segment7Tall"},
    })

struct Clock {
    using serialize = zpp::bits::members<10>;

    double horizontalScale = 1.1;
    double verticalScale = 2.0;
    uint8_t timezoneIndex = 2;
    ClockFont font = ClockFont::Segment7Tall;
    bool showSeconds = true;
    bool autoScale = true;
    uint32_t targetDisplayWidth = 752;
    uint32_t targetDisplayHeight = 480;
    uint32_t marginPixels = 20;
    double eventFrequency = 0.5;
};

} // namespace DirtSim::Config
