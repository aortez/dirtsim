#pragma once

#include "../MaterialType.h"
#include "clock_scenario/GlowConfig.h"

#include <zpp_bits.h>

namespace DirtSim::Config {

enum class ClockFont : uint8_t {
    DotMatrix = 0,
    Montserrat24 = 1,
    NotoColorEmoji = 2,
    Segment7 = 3,
    Segment7ExtraTall = 4,
    Segment7Jumbo = 5,
    Segment7Large = 6,
    Segment7Tall = 7,
};

// Returns the display name for a ClockFont value.
const char* getDisplayName(ClockFont font);

struct Clock {
    using serialize = zpp::bits::members<21>;

    bool autoScale = true;
    bool colorCycleEnabled = false;
    bool colorShowcaseEnabled = false;
    bool digitSlideEnabled = false;
    bool duckEnabled = false;
    bool marqueeEnabled = false;
    bool meltdownEnabled = false;
    bool rainEnabled = false;
    bool showSeconds = false;
    ClockFont font = ClockFont::DotMatrix;
    double colorsPerSecond = 4.0;
    double eventFrequency = 0.5;
    double horizontalScale = 1.1;
    double verticalScale = 2.0;
    GlowConfig glowConfig;
    Material::EnumType digitMaterial = Material::EnumType::Metal;
    uint32_t marginPixels = 20;
    uint32_t targetDisplayHeight = 480;
    uint32_t targetDisplayWidth = 752;
    uint8_t digitEmissiveness = 10;
    uint8_t timezoneIndex = 2;
};

} // namespace DirtSim::Config
