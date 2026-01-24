#include "ScenarioMetadataCache.h"
#include <cassert>
#include <sstream>

namespace DirtSim {
namespace Ui {

std::vector<Api::ScenarioListGet::ScenarioInfo> ScenarioMetadataCache::scenarios_;

void ScenarioMetadataCache::load(const std::vector<Api::ScenarioListGet::ScenarioInfo>& scenarios)
{
    scenarios_ = scenarios;
}

bool ScenarioMetadataCache::hasScenarios()
{
    return !scenarios_.empty();
}

std::string ScenarioMetadataCache::buildDropdownOptions()
{
    assert(!scenarios_.empty() && "ScenarioMetadataCache::load() must be called first");

    std::ostringstream oss;
    for (size_t i = 0; i < scenarios_.size(); ++i) {
        if (i > 0) {
            oss << "\n";
        }
        oss << scenarios_[i].name;
    }
    return oss.str();
}

std::vector<std::string> ScenarioMetadataCache::buildOptionsList()
{
    assert(!scenarios_.empty() && "ScenarioMetadataCache::load() must be called first");

    std::vector<std::string> options;
    for (const auto& scenario : scenarios_) {
        options.push_back(scenario.name);
    }
    return options;
}

Scenario::EnumType ScenarioMetadataCache::scenarioIdFromIndex(uint16_t index)
{
    assert(!scenarios_.empty() && "ScenarioMetadataCache::load() must be called first");
    assert(index < scenarios_.size() && "Scenario index out of range");

    return scenarios_[index].id;
}

uint16_t ScenarioMetadataCache::indexFromScenarioId(Scenario::EnumType id)
{
    for (size_t i = 0; i < scenarios_.size(); ++i) {
        if (scenarios_[i].id == id) {
            return static_cast<uint16_t>(i);
        }
    }
    assert(false && "Scenario ID not found in cache");
    return 0;
}

std::optional<Api::ScenarioListGet::ScenarioInfo> ScenarioMetadataCache::getScenarioInfo(
    Scenario::EnumType id)
{
    if (scenarios_.empty()) {
        return std::nullopt;
    }

    for (const auto& scenario : scenarios_) {
        if (scenario.id == id) {
            return scenario;
        }
    }

    return std::nullopt;
}

} // namespace Ui
} // namespace DirtSim
