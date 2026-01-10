#include "DuckSensoryData.h"

namespace DirtSim {

void to_json(nlohmann::json& j, const DuckSensoryData& data)
{
    j = nlohmann::json{ { "material_histograms", data.material_histograms },
                        { "actual_width", data.actual_width },
                        { "actual_height", data.actual_height },
                        { "scale_factor", data.scale_factor },
                        { "world_offset", data.world_offset },
                        { "position", data.position },
                        { "velocity", data.velocity },
                        { "on_ground", data.on_ground },
                        { "facing_x", data.facing_x },
                        { "delta_time_seconds", data.delta_time_seconds } };
}

void from_json(const nlohmann::json& j, DuckSensoryData& data)
{
    j.at("material_histograms").get_to(data.material_histograms);
    j.at("actual_width").get_to(data.actual_width);
    j.at("actual_height").get_to(data.actual_height);
    j.at("scale_factor").get_to(data.scale_factor);
    j.at("world_offset").get_to(data.world_offset);
    j.at("position").get_to(data.position);
    j.at("velocity").get_to(data.velocity);
    j.at("on_ground").get_to(data.on_ground);
    j.at("facing_x").get_to(data.facing_x);
    j.at("delta_time_seconds").get_to(data.delta_time_seconds);
}

} // namespace DirtSim
