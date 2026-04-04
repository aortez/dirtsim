#pragma once

namespace DirtSim {
namespace Server {

class StateMachine;

namespace SearchSupport {
class SmbPlanExecution;
} // namespace SearchSupport

namespace State {

/// Broadcast a render message from an SmbPlanExecution to all connected clients.
void broadcastExecutionRender(StateMachine& dsm, const SearchSupport::SmbPlanExecution& execution);

} // namespace State
} // namespace Server
} // namespace DirtSim
