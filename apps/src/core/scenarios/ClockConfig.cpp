#include "ClockConfig.h"
#include "core/ReflectSerializer.h"

namespace DirtSim::Config {

const char* getDisplayName(ClockFont font)
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

void from_json(const nlohmann::json& j, Clock& config)
{
    config = ReflectSerializer::from_json<Clock>(j);
}

void to_json(nlohmann::json& j, const Clock& config)
{
    j = ReflectSerializer::to_json(config);
}

} // namespace DirtSim::Config
