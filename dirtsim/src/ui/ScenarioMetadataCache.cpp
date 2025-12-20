#include "ScenarioMetadataCache.h"
#include <cstdint>
#include <sstream>

namespace DirtSim {
namespace Ui {

std::string ScenarioMetadataCache::buildDropdownOptions()
{
    std::ostringstream oss;
    for (size_t i = 0; i < SCENARIO_METADATA.size(); ++i) {
        if (i > 0) {
            oss << "\n";
        }
        oss << SCENARIO_METADATA[i].name;
    }
    return oss.str();
}

std::string ScenarioMetadataCache::scenarioIdFromIndex(uint16_t index)
{
    if (index < SCENARIO_METADATA.size()) {
        return std::string(SCENARIO_METADATA[index].id);
    }
    return "sandbox"; // Default fallback.
}

uint16_t ScenarioMetadataCache::indexFromScenarioId(const std::string& id)
{
    return static_cast<uint16_t>(getScenarioIndex(id));
}

} // namespace Ui
} // namespace DirtSim
