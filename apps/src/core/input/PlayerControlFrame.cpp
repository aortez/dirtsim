#include "PlayerControlFrame.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const PlayerControlFrame& value)
{
    j = nlohmann::json{
        { "xAxis", value.xAxis },
        { "yAxis", value.yAxis },
        { "buttons", value.buttons },
    };
}

void from_json(const nlohmann::json& j, PlayerControlFrame& value)
{
    j.at("xAxis").get_to(value.xAxis);
    j.at("yAxis").get_to(value.yAxis);
    j.at("buttons").get_to(value.buttons);
}

} // namespace DirtSim
