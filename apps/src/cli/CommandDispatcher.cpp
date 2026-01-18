#include "CommandDispatcher.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Client {

CommandDispatcher::CommandDispatcher()
{
    spdlog::debug(
        "CommandDispatcher: Registering server API commands with response deserializers...");

    // Register server API commands.
    registerCommand<Api::CellGet::Cwc>(serverHandlers_);
    registerCommand<Api::CellSet::Cwc>(serverHandlers_);
    registerCommand<Api::ClockEventTrigger::Cwc>(serverHandlers_);
    registerCommand<Api::DiagramGet::Cwc>(serverHandlers_);
    registerCommand<Api::EvolutionStart::Cwc>(serverHandlers_);
    registerCommand<Api::EvolutionStop::Cwc>(serverHandlers_);
    registerCommand<Api::Exit::Cwc>(serverHandlers_);
    registerCommand<Api::FingerDown::Cwc>(serverHandlers_);
    registerCommand<Api::FingerMove::Cwc>(serverHandlers_);
    registerCommand<Api::FingerUp::Cwc>(serverHandlers_);
    registerCommand<Api::GenomeDelete::Cwc>(serverHandlers_);
    registerCommand<Api::GenomeGet::Cwc>(serverHandlers_);
    registerCommand<Api::GenomeGetBest::Cwc>(serverHandlers_);
    registerCommand<Api::GenomeList::Cwc>(serverHandlers_);
    registerCommand<Api::GenomeSet::Cwc>(serverHandlers_);
    registerCommand<Api::GravitySet::Cwc>(serverHandlers_);
    registerCommand<Api::PeersGet::Cwc>(serverHandlers_);
    registerCommand<Api::PerfStatsGet::Cwc>(serverHandlers_);
    registerCommand<Api::PhysicsSettingsGet::Cwc>(serverHandlers_);
    registerCommand<Api::PhysicsSettingsSet::Cwc>(serverHandlers_);
    registerCommand<Api::RenderFormatGet::Cwc>(serverHandlers_);
    registerCommand<Api::RenderFormatSet::Cwc>(serverHandlers_);
    registerCommand<Api::Reset::Cwc>(serverHandlers_);
    registerCommand<Api::ScenarioConfigSet::Cwc>(serverHandlers_);
    registerCommand<Api::ScenarioListGet::Cwc>(serverHandlers_);
    registerCommand<Api::SeedAdd::Cwc>(serverHandlers_);
    registerCommand<Api::SimRun::Cwc>(serverHandlers_);
    registerCommand<Api::SimStop::Cwc>(serverHandlers_);
    registerCommand<Api::SpawnDirtBall::Cwc>(serverHandlers_);
    registerCommand<Api::StateGet::Cwc>(serverHandlers_);
    registerCommand<Api::StatusGet::Cwc>(serverHandlers_);
    registerCommand<Api::TimerStatsGet::Cwc>(serverHandlers_);
    registerCommand<Api::TrainingResultDiscard::Cwc>(serverHandlers_);
    registerCommand<Api::TrainingResultSave::Cwc>(serverHandlers_);
    registerCommand<Api::WorldResize::Cwc>(serverHandlers_);

    spdlog::debug("CommandDispatcher: Registering UI API commands...");

    // Register UI API commands.
    registerCommand<UiApi::DrawDebugToggle::Cwc>(uiHandlers_);
    registerCommand<UiApi::Exit::Cwc>(uiHandlers_);
    registerCommand<UiApi::MouseDown::Cwc>(uiHandlers_);
    registerCommand<UiApi::MouseMove::Cwc>(uiHandlers_);
    registerCommand<UiApi::MouseUp::Cwc>(uiHandlers_);
    registerCommand<UiApi::PixelRendererToggle::Cwc>(uiHandlers_);
    registerCommand<UiApi::RenderModeSelect::Cwc>(uiHandlers_);
    registerCommand<UiApi::ScreenGrab::Cwc>(uiHandlers_);
    registerCommand<UiApi::SimPause::Cwc>(uiHandlers_);
    registerCommand<UiApi::SimRun::Cwc>(uiHandlers_);
    registerCommand<UiApi::SimStop::Cwc>(uiHandlers_);
    registerCommand<UiApi::StateGet::Cwc>(uiHandlers_);
    registerCommand<UiApi::StatusGet::Cwc>(uiHandlers_);
    registerCommand<UiApi::StreamStart::Cwc>(uiHandlers_);
    registerCommand<UiApi::WebRtcAnswer::Cwc>(uiHandlers_);
    registerCommand<UiApi::WebRtcCandidate::Cwc>(uiHandlers_);

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
