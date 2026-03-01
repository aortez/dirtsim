#pragma once

#include "core/ScenarioMetadata.h"

#include <cstdint>
#include <vector>

namespace DirtSim::Network {
class WebSocketServiceInterface;
}

namespace DirtSim::Ui {

class ScenarioMetadataManager {
public:
    void syncFromServer(Network::WebSocketServiceInterface& wsService, int timeoutMs = 2000);

    const std::vector<ScenarioMetadata>& scenarios() const;
    const ScenarioMetadata& get(Scenario::EnumType scenarioId) const;

private:
    std::vector<ScenarioMetadata> scenarios_;
};

} // namespace DirtSim::Ui
