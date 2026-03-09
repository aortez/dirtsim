#include "DebugVisualizationMode.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace DirtSim {
namespace Ui {

std::string debugVisualizationModeToString(DebugVisualizationMode mode)
{
    switch (mode) {
        case DebugVisualizationMode::Combined:
            return "combined";
        case DebugVisualizationMode::LivePressure:
            return "live_pressure";
        case DebugVisualizationMode::RegionActivity:
            return "region_activity";
        case DebugVisualizationMode::StaticLoad:
            return "static_load";
        default:
            return "combined";
    }
}

DebugVisualizationMode stringToDebugVisualizationMode(const std::string& str)
{
    if (str == "combined") return DebugVisualizationMode::Combined;
    if (str == "live_pressure") return DebugVisualizationMode::LivePressure;
    if (str == "region_activity") return DebugVisualizationMode::RegionActivity;
    if (str == "static_load") return DebugVisualizationMode::StaticLoad;
    return DebugVisualizationMode::Combined;
}

void to_json(nlohmann::json& j, DebugVisualizationMode mode)
{
    j = debugVisualizationModeToString(mode);
}

void from_json(const nlohmann::json& j, DebugVisualizationMode& mode)
{
    if (!j.is_string()) {
        throw std::runtime_error("DebugVisualizationMode::from_json: JSON value must be a string");
    }

    mode = stringToDebugVisualizationMode(j.get<std::string>());
}

} // namespace Ui
} // namespace DirtSim
