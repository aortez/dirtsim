#pragma once

#include "core/Result.h"
#include "core/ScenarioConfig.h"
#include "core/ScenarioId.h"
#include "core/Vector2.h"
#include "server/api/ApiError.h"

#include <variant>

namespace DirtSim::Server {
class StateMachine;
namespace State {
struct SimRunning;
}
} // namespace DirtSim::Server

namespace DirtSim::Server::State {

Result<std::monostate, ApiError> startScenarioSession(
    StateMachine& dsm,
    SimRunning& state,
    Scenario::EnumType scenarioId,
    const ScenarioConfig& scenarioConfig,
    const Vector2s& containerSize);

} // namespace DirtSim::Server::State
