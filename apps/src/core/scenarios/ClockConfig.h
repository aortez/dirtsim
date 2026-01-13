#pragma once

#include "../MaterialType.h"
#include "../ReflectEnumJson.h"

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
// Add new entries here when adding fonts to the enum.
inline const char* getDisplayName(ClockFont font)
{
    switch (font) {
        case ClockFont::DotMatrix:
            return "Dot Matrix";
        case ClockFont::Montserrat24:
            return "Montserrat 24";
        case ClockFont::NotoColorEmoji:
            return "Noto Emoji";
        case ClockFont::Segment7:
            return "7-Segment";
        case ClockFont::Segment7ExtraTall:
            return "7-Seg X-Tall";
        case ClockFont::Segment7Jumbo:
            return "7-Seg Jumbo";
        case ClockFont::Segment7Large:
            return "7-Seg Large";
        case ClockFont::Segment7Tall:
            return "7-Seg Tall";
    }
    return "Unknown";
}

struct Clock {
    using serialize = zpp::bits::members<20>;

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
    double colorsPerSecond = 4.0; // Color cycle rate when colorCycleEnabled is true.
    bool colorCycleEnabled = false;
    bool colorShowcaseEnabled = false;
    bool digitSlideEnabled = false;
    bool duckEnabled = false;
    bool marqueeEnabled = false;
    bool meltdownEnabled = false;
    bool rainEnabled = false;
    Material::EnumType digitMaterial = Material::EnumType::METAL; // Render color for clock digits.
    uint8_t digitEmissiveness = 10;                               // Digit glow intensity (0-20).
};

} // namespace DirtSim::Config
