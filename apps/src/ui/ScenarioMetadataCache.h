#pragma once

#include "server/api/ScenarioListGet.h"
#include <cstdint>
#include <optional>
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
     * @brief Check if scenario metadata has been loaded.
     */
    static bool hasScenarios();

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

    /**
     * @brief Lookup scenario info by ID.
     */
    static std::optional<Api::ScenarioListGet::ScenarioInfo> getScenarioInfo(Scenario::EnumType id);

private:
    static std::vector<Api::ScenarioListGet::ScenarioInfo> scenarios_;
};

} // namespace Ui
} // namespace DirtSim
