#include "SearchBroadcastHelpers.h"
#include "core/ScenarioConfig.h"
#include "server/StateMachine.h"
#include "server/search/SmbPlanExecution.h"
#include <vector>

namespace DirtSim {
namespace Server {
namespace State {

void broadcastExecutionRender(StateMachine& dsm, const SearchSupport::SmbPlanExecution& execution)
{
    static const std::vector<OrganismId> emptyOrganismGrid{};
    const auto scenarioId = Scenario::EnumType::NesSuperMarioBros;
    const auto scenarioConfig = makeDefaultConfig(scenarioId);
    dsm.broadcastRenderMessage(
        execution.getWorldData(),
        emptyOrganismGrid,
        scenarioId,
        scenarioConfig,
        std::nullopt,
        execution.getScenarioVideoFrame());
}

} // namespace State
} // namespace Server
} // namespace DirtSim
