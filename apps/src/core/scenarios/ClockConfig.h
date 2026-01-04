#pragma once

#include "../MaterialType.h"
#include "../ReflectEnumJson.h"

#include <zpp_bits.h>

namespace DirtSim::Config {

enum class ClockFont : uint8_t {
    DotMatrix = 0,
    Segment7 = 1,
    Segment7ExtraTall = 2,
    Segment7Jumbo = 3,
    Segment7Large = 4,
    Segment7Tall = 5,
};

struct Clock {
    using serialize = zpp::bits::members<17>;

    double horizontalScale = 1.1;
    double verticalScale = 2.0;
    uint8_t timezoneIndex = 2;
    ClockFont font = ClockFont::DotMatrix;
    bool showSeconds = false;
    bool autoScale = true;
    uint32_t targetDisplayWidth = 752;
    uint32_t targetDisplayHeight = 480;
    uint32_t marginPixels = 20;
    double eventFrequency = 0.5;
    double colorsPerSecond = 4.0;  // Color cycle rate when colorCycleEnabled is true.
    bool colorCycleEnabled = false;
    bool colorShowcaseEnabled = false;
    bool duckEnabled = false;
    bool meltdownEnabled = false;
    bool rainEnabled = false;
    MaterialType digitMaterial = MaterialType::METAL;  // Render color for clock digits.
};

} // namespace DirtSim::Config
