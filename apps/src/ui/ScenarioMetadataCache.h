#pragma once

#include "server/api/ScenarioListGet.h"
#include <cstdint>
#include <string>
#include <vector>

namespace DirtSim {
namespace Ui {

/**
 * @brief Helper for building UI dropdowns from scenario metadata.
 * Caches scenario list fetched from server at runtime.
 */
class ScenarioMetadataCache {
public:
    /**
     * @brief Load scenario list from server response.
     */
    static void load(const std::vector<Api::ScenarioListGet::ScenarioInfo>& scenarios);

    /**
     * @brief Build dropdown options string ("Name1\nName2\n...").
     */
    static std::string buildDropdownOptions();

    /**
     * @brief Build options list for radio panels.
     */
    static std::vector<std::string> buildOptionsList();

    /**
     * @brief Map dropdown index to scenario ID.
     */
    static Scenario::EnumType scenarioIdFromIndex(uint16_t index);

    /**
     * @brief Map scenario ID to dropdown index.
     */
    static uint16_t indexFromScenarioId(Scenario::EnumType id);

private:
    static std::vector<Api::ScenarioListGet::ScenarioInfo> scenarios_;
};

} // namespace Ui
} // namespace DirtSim
