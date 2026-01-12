#include "State.h"
#include "core/ConfigLoader.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "server/ServerConfig.h"
#include "server/StateMachine.h"

namespace DirtSim {
namespace Server {
namespace State {

Any Startup::onEnter(StateMachine& dsm)
{
    LOG_INFO(State, "Loading server configuration");

    auto configResult = ConfigLoader::load<ServerConfig>("server.json");
    if (configResult.isError()) {
        LOG_ERROR(State, "Failed to load config: {}", configResult.errorValue());
        return Error{ .error_message = configResult.errorValue() };
    }

    dsm.serverConfig = std::make_unique<ServerConfig>(configResult.value());

    Scenario::EnumType startupScenario = getScenarioId(dsm.serverConfig->startupConfig);
    LOG_INFO(State, "Startup scenario: {}", toString(startupScenario));
    LOG_INFO(State, "Transitioning to Idle");

    return Idle{};
}

} // namespace State
} // namespace Server
} // namespace DirtSim