#include "SearchBroadcastHelpers.h"
#include "core/ScenarioConfig.h"
#include "server/StateMachine.h"
#include "server/search/SmbPlanExecution.h"
#include <vector>

namespace DirtSim {
namespace Server {
namespace State {

void broadcastSearchRender(
    StateMachine& dsm, const WorldData& worldData, const std::optional<ScenarioVideoFrame>& frame)
{
    static const std::vector<OrganismId> emptyOrganismGrid{};
    const auto scenarioId = Scenario::EnumType::NesSuperMarioBros;
    const auto scenarioConfig = makeDefaultConfig(scenarioId);
    dsm.broadcastRenderMessage(
        worldData, emptyOrganismGrid, scenarioId, scenarioConfig, std::nullopt, frame);
}

void broadcastExecutionRender(StateMachine& dsm, const SearchSupport::SmbPlanExecution& execution)
{
    broadcastSearchRender(dsm, execution.getWorldData(), execution.getScenarioVideoFrame());
}

} // namespace State
} // namespace Server
} // namespace DirtSim
