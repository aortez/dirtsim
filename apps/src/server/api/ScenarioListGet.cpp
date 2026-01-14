#include "ScenarioListGet.h"

namespace DirtSim {
namespace Api {
namespace ScenarioListGet {

nlohmann::json Command::toJson() const
{
    return nlohmann::json{ { "command", "scenario_list_get" } };
}

Command Command::fromJson(const nlohmann::json&)
{
    return Command{};
}

nlohmann::json Okay::toJson() const
{
    nlohmann::json result;
    result["scenarios"] = nlohmann::json::array();

    for (const auto& scenario : scenarios) {
        result["scenarios"].push_back({ { "id", scenario.id },
                                        { "name", scenario.name },
                                        { "description", scenario.description },
                                        { "category", scenario.category } });
    }

    return result;
}

} // namespace ScenarioListGet
} // namespace Api
} // namespace DirtSim
