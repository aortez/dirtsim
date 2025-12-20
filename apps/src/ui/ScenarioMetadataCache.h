#pragma once

#include "core/ScenarioMetadata.h"
#include <cstdint>
#include <string>

namespace DirtSim {
namespace Ui {

/**
 * @brief Helper for building UI dropdowns from scenario metadata.
 * Wraps the static SCENARIO_METADATA array with UI-specific helpers.
 */
class ScenarioMetadataCache {
public:
    /**
     * @brief Build dropdown options string ("Name1\nName2\n...").
     */
    static std::string buildDropdownOptions();

    /**
     * @brief Map dropdown index to scenario ID.
     * @return Scenario ID, or "sandbox" if index out of range.
     */
    static std::string scenarioIdFromIndex(uint16_t index);

    /**
     * @brief Map scenario ID to dropdown index.
     * @return Index, or 0 if ID not found.
     */
    static uint16_t indexFromScenarioId(const std::string& id);
};

} // namespace Ui
} // namespace DirtSim
