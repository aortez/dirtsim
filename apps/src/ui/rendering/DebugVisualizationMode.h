#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace DirtSim {
namespace Ui {

enum class DebugVisualizationMode {
    Combined,
    LivePressure,
    RegionActivity,
    StaticLoad,
};

std::string debugVisualizationModeToString(DebugVisualizationMode mode);
DebugVisualizationMode stringToDebugVisualizationMode(const std::string& str);

void to_json(nlohmann::json& j, DebugVisualizationMode mode);
void from_json(const nlohmann::json& j, DebugVisualizationMode& mode);

} // namespace Ui
} // namespace DirtSim
