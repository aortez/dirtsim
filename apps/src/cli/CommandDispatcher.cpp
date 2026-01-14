#include "CommandDispatcher.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Client {

CommandDispatcher::CommandDispatcher()
{
    spdlog::debug(
        "CommandDispatcher: Registering server API commands with response deserializers...");

    // Register server API commands.
    registerCommand<Api::CellGet::Command, Api::CellGet::Okay>(serverHandlers_);
    registerCommand<Api::CellSet::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::DiagramGet::Command, Api::DiagramGet::Okay>(serverHandlers_);
    registerCommand<Api::EvolutionStart::Command, Api::EvolutionStart::Okay>(serverHandlers_);
    registerCommand<Api::EvolutionStop::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::Exit::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::FingerDown::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::FingerMove::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::FingerUp::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::GenomeGet::Command, Api::GenomeGet::Okay>(serverHandlers_);
    registerCommand<Api::GenomeGetBest::Command, Api::GenomeGetBest::Okay>(serverHandlers_);
    registerCommand<Api::GenomeList::Command, Api::GenomeList::Okay>(serverHandlers_);
    registerCommand<Api::GenomeSet::Command, Api::GenomeSet::Okay>(serverHandlers_);
    registerCommand<Api::GravitySet::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::PeersGet::Command, Api::PeersGet::Okay>(serverHandlers_);
    registerCommand<Api::PerfStatsGet::Command, Api::PerfStatsGet::Okay>(serverHandlers_);
    registerCommand<Api::PhysicsSettingsGet::Command, Api::PhysicsSettingsGet::Okay>(
        serverHandlers_);
    registerCommand<Api::PhysicsSettingsSet::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::RenderFormatGet::Command, Api::RenderFormatGet::Okay>(serverHandlers_);
    registerCommand<Api::RenderFormatSet::Command, Api::RenderFormatSet::Okay>(serverHandlers_);
    registerCommand<Api::Reset::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::ScenarioConfigSet::Command, Api::ScenarioConfigSet::Okay>(serverHandlers_);
    registerCommand<Api::ScenarioListGet::Command, Api::ScenarioListGet::Okay>(serverHandlers_);
    registerCommand<Api::SeedAdd::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::SimRun::Command, Api::SimRun::Okay>(serverHandlers_);
    registerCommand<Api::SimStop::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::SpawnDirtBall::Command, std::monostate>(serverHandlers_);
    registerCommand<Api::StateGet::Command, Api::StateGet::Okay>(serverHandlers_);
    registerCommand<Api::StatusGet::Command, Api::StatusGet::Okay>(serverHandlers_);
    registerCommand<Api::TimerStatsGet::Command, Api::TimerStatsGet::Okay>(serverHandlers_);
    registerCommand<Api::WorldResize::Command, std::monostate>(serverHandlers_);

    spdlog::debug("CommandDispatcher: Registering UI API commands...");

    // Register UI API commands.
    registerCommand<UiApi::DrawDebugToggle::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::Exit::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::MouseDown::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::MouseMove::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::MouseUp::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::PixelRendererToggle::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::RenderModeSelect::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::ScreenGrab::Command, UiApi::ScreenGrab::Okay>(uiHandlers_);
    registerCommand<UiApi::SimPause::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::SimRun::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::SimStop::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::StatusGet::Command, UiApi::StatusGet::Okay>(uiHandlers_);
    registerCommand<UiApi::StreamStart::Command, UiApi::StreamStart::Okay>(uiHandlers_);
    registerCommand<UiApi::WebRtcAnswer::Command, std::monostate>(uiHandlers_);
    registerCommand<UiApi::WebRtcCandidate::Command, std::monostate>(uiHandlers_);

    spdlog::info(
        "CommandDispatcher: Registered {} server commands, {} UI commands",
        serverHandlers_.size(),
        uiHandlers_.size());
}

const CommandDispatcher::HandlerMap& CommandDispatcher::getHandlers(Target target) const
{
    return (target == Target::Server) ? serverHandlers_ : uiHandlers_;
}

Result<std::string, ApiError> CommandDispatcher::dispatch(
    Target target,
    Network::WebSocketService& client,
    const std::string& commandName,
    const nlohmann::json& body)
{
    const auto& handlers = getHandlers(target);
    auto it = handlers.find(commandName);
    if (it == handlers.end()) {
        return Result<std::string, ApiError>::error(ApiError{ "Unknown command: " + commandName });
    }

    spdlog::debug(
        "CommandDispatcher: Dispatching {} command '{}'",
        target == Target::Server ? "server" : "UI",
        commandName);
    return it->second(client, body);
}

bool CommandDispatcher::hasCommand(Target target, const std::string& commandName) const
{
    const auto& handlers = getHandlers(target);
    return handlers.find(commandName) != handlers.end();
}

std::vector<std::string> CommandDispatcher::getCommandNames(Target target) const
{
    const auto& handlers = getHandlers(target);
    std::vector<std::string> names;
    names.reserve(handlers.size());
    for (const auto& [name, handler] : handlers) {
        names.push_back(name);
    }
    return names;
}

} // namespace Client
} // namespace DirtSim
