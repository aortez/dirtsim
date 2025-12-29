#include "ScenarioConfigSet.h"

namespace DirtSim {
namespace Api {
namespace ScenarioConfigSet {

nlohmann::json Command::toJson() const
{
    nlohmann::json j;
    DirtSim::to_json(j["config"], config);
    return j;
}

Command Command::fromJson(const nlohmann::json& j)
{
    Command cmd;
    if (j.contains("config")) {
        DirtSim::from_json(j["config"], cmd.config);
    }
    return cmd;
}

nlohmann::json Okay::toJson() const
{
    nlohmann::json j;
    j["success"] = success;
    return j;
}

} // namespace ScenarioConfigSet
} // namespace Api
} // namespace DirtSim
