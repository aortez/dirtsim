#include "ui/ScenarioMetadataManager.h"

#include "core/Assert.h"
#include "core/network/WebSocketServiceInterface.h"
#include "server/api/ScenarioListGet.h"
#include <cstdlib>

namespace DirtSim::Ui {

void ScenarioMetadataManager::syncFromServer(
    Network::WebSocketServiceInterface& wsService, int timeoutMs)
{
    DIRTSIM_ASSERT(wsService.isConnected(), "ScenarioMetadataManager: WebSocket not connected");

    const Api::ScenarioListGet::Command cmd{};
    const auto result =
        wsService.sendCommandAndGetResponse<Api::ScenarioListGet::Okay>(cmd, timeoutMs);
    DIRTSIM_ASSERT(!result.isError(), "ScenarioListGet failed: " + result.errorValue());
    DIRTSIM_ASSERT(
        !result.value().isError(),
        "ScenarioListGet rejected: " + result.value().errorValue().message);

    scenarios_ = result.value().value().scenarios;
}

const std::vector<ScenarioMetadata>& ScenarioMetadataManager::scenarios() const
{
    DIRTSIM_ASSERT(!scenarios_.empty(), "ScenarioMetadataManager: scenario list not loaded");
    return scenarios_;
}

const ScenarioMetadata& ScenarioMetadataManager::get(Scenario::EnumType scenarioId) const
{
    for (const auto& scenarioMetadata : scenarios()) {
        if (scenarioMetadata.id == scenarioId) {
            return scenarioMetadata;
        }
    }
    DIRTSIM_ASSERT(
        false,
        "ScenarioMetadataManager: missing scenario metadata for id "
            + Scenario::toString(scenarioId));
    std::abort();
}

} // namespace DirtSim::Ui
