#pragma once

#include "core/RenderMessage.h"
#include "core/WorldData.h"

#include <optional>

namespace DirtSim {
namespace Server {

class StateMachine;

namespace SearchSupport {
class SmbPlanExecution;
} // namespace SearchSupport

namespace State {

void broadcastSearchRender(
    StateMachine& dsm, const WorldData& worldData, const std::optional<ScenarioVideoFrame>& frame);

/// Broadcast a render message from an SmbPlanExecution to all connected clients.
void broadcastExecutionRender(StateMachine& dsm, const SearchSupport::SmbPlanExecution& execution);

} // namespace State
} // namespace Server
} // namespace DirtSim
