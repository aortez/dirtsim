#include "SearchHelpers.h"
#include "core/LoggingChannels.h"
#include "core/network/WebSocketService.h"
#include "server/api/PlanPlaybackStart.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/SearchStart.h"
#include "ui/state-machine/StateMachine.h"

namespace DirtSim {
namespace Ui {
namespace SearchHelpers {

Result<std::monostate, std::string> startPlanPlayback(StateMachine& sm, UUID planId)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        return Result<std::monostate, std::string>::error("UI is not connected to the server");
    }

    Api::PlanPlaybackStart::Command command{
        .planId = planId,
    };
    const auto result = wsService.sendCommandAndGetResponse<Api::PlanPlaybackStart::OkayType>(
        command, kServerTimeoutMs);
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(result.errorValue());
    }
    if (result.value().isError()) {
        return Result<std::monostate, std::string>::error(result.value().errorValue().message);
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

Result<std::monostate, std::string> startSearch(StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        return Result<std::monostate, std::string>::error("UI is not connected to the server");
    }

    Api::SearchStart::Command command{};
    const auto result =
        wsService.sendCommandAndGetResponse<Api::SearchStart::OkayType>(command, kServerTimeoutMs);
    if (result.isError()) {
        return Result<std::monostate, std::string>::error(result.errorValue());
    }
    if (result.value().isError()) {
        return Result<std::monostate, std::string>::error(result.value().errorValue().message);
    }

    return Result<std::monostate, std::string>::okay(std::monostate{});
}

void subscribeToBasicRender(StateMachine& sm)
{
    auto& wsService = sm.getWebSocketService();
    if (!wsService.isConnected()) {
        LOG_WARN(State, "Not connected to server, skipping render subscription");
        return;
    }

    Api::RenderFormatSet::Command command{
        .format = RenderFormat::EnumType::Basic,
        .connectionId = "",
    };
    const auto result =
        wsService.sendCommandAndGetResponse<Api::RenderFormatSet::OkayType>(command, 250);
    if (result.isError()) {
        LOG_WARN(State, "Failed to subscribe to render stream: {}", result.errorValue());
        return;
    }
    if (result.value().isError()) {
        LOG_WARN(State, "RenderFormatSet rejected: {}", result.value().errorValue().message);
    }
}

} // namespace SearchHelpers
} // namespace Ui
} // namespace DirtSim
