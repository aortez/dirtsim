#include "CommandDispatcher.h"
#include "audio/api/MasterVolumeSet.h"
#include "audio/api/NoteOff.h"
#include "audio/api/NoteOn.h"
#include "audio/api/StatusGet.h"
#include "os-manager/api/PeerClientKeyEnsure.h"
#include "os-manager/api/PeersGet.h"
#include "os-manager/api/Reboot.h"
#include "os-manager/api/RemoteCliRun.h"
#include "os-manager/api/RestartAudio.h"
#include "os-manager/api/RestartServer.h"
#include "os-manager/api/RestartUi.h"
#include "os-manager/api/StartAudio.h"
#include "os-manager/api/StartServer.h"
#include "os-manager/api/StartUi.h"
#include "os-manager/api/StopAudio.h"
#include "os-manager/api/StopServer.h"
#include "os-manager/api/StopUi.h"
#include "os-manager/api/SystemStatus.h"
#include "os-manager/api/TrustBundleGet.h"
#include "os-manager/api/TrustPeer.h"
#include "os-manager/api/UntrustPeer.h"
#include "os-manager/api/WebSocketAccessSet.h"
#include "os-manager/api/WebUiAccessSet.h"
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Client {

CommandDispatcher::CommandDispatcher()
{
    spdlog::debug(
        "CommandDispatcher: Registering audio API commands with response deserializers...");

    registerCommand<AudioApi::NoteOff::Cwc>(audioHandlers_, audioExampleHandlers_);
    registerCommand<AudioApi::NoteOn::Cwc>(audioHandlers_, audioExampleHandlers_);
    registerCommand<AudioApi::MasterVolumeSet::Cwc>(audioHandlers_, audioExampleHandlers_);
    registerCommand<AudioApi::StatusGet::Cwc>(audioHandlers_, audioExampleHandlers_);

    spdlog::debug(
        "CommandDispatcher: Registering server API commands with response deserializers...");

    // Register server API commands.
    registerCommand<Api::CellGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::CellSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::ClockEventTrigger::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::DiagramGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::EventSubscribe::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::EvolutionStart::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::EvolutionStop::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::Exit::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::FingerDown::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::FingerMove::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::FingerUp::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::GenomeDelete::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::GenomeGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::GenomeList::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::GenomeSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::GravitySet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::NesInputSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::PerfStatsGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::PhysicsSettingsGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::PhysicsSettingsSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::RenderFormatGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::RenderFormatSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::RenderStreamConfigSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::Reset::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::ScenarioListGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::ScenarioSwitch::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::SeedAdd::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::SimRun::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::SimStop::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::SpawnDirtBall::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::StateGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::StatusGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::TimerStatsGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::UserSettingsGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::UserSettingsPatch::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::UserSettingsReset::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::UserSettingsSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::TrainingResultDiscard::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::TrainingResultDelete::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::TrainingResultGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::TrainingResultList::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::TrainingResultSave::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::TrainingResultSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::TrainingBestSnapshotGet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::WebSocketAccessSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::WebUiAccessSet::Cwc>(serverHandlers_, serverExampleHandlers_);
    registerCommand<Api::WorldResize::Cwc>(serverHandlers_, serverExampleHandlers_);

    spdlog::debug("CommandDispatcher: Registering UI API commands...");

    // Register UI API commands.
    registerCommand<UiApi::DrawDebugToggle::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::Exit::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::GenomeBrowserOpen::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::GenomeDetailLoad::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::GenomeDetailOpen::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::IconRailExpand::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::IconRailShowIcons::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::IconSelect::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::MouseDown::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::MouseMove::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::MouseUp::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::PixelRendererToggle::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::RenderModeSelect::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::ScreenGrab::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::SimPause::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::SimRun::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::SimStop::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::StateGet::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::StatusGet::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::StopButtonPress::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::StreamStart::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::SynthKeyEvent::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::TrainingConfigShowEvolution::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::TrainingQuit::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::TrainingResultDiscard::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::TrainingResultSave::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::TrainingStart::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::WebSocketAccessSet::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::WebRtcAnswer::Cwc>(uiHandlers_, uiExampleHandlers_);
    registerCommand<UiApi::WebRtcCandidate::Cwc>(uiHandlers_, uiExampleHandlers_);

    spdlog::debug("CommandDispatcher: Registering OS manager API commands...");

    registerCommand<OsApi::Reboot::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::PeerClientKeyEnsure::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::PeersGet::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::RemoteCliRun::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::RestartAudio::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::RestartServer::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::RestartUi::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::StartAudio::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::StartServer::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::StartUi::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::StopAudio::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::StopServer::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::StopUi::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::SystemStatus::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::TrustBundleGet::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::TrustPeer::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::UntrustPeer::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::WebSocketAccessSet::Cwc>(osHandlers_, osExampleHandlers_);
    registerCommand<OsApi::WebUiAccessSet::Cwc>(osHandlers_, osExampleHandlers_);

    spdlog::info(
        "CommandDispatcher: Registered {} audio commands, {} server commands, {} UI commands, {} "
        "OS commands",
        audioHandlers_.size(),
        serverHandlers_.size(),
        uiHandlers_.size(),
        osHandlers_.size());
}

const CommandDispatcher::HandlerMap& CommandDispatcher::getHandlers(Target target) const
{
    if (target == Target::Audio) {
        return audioHandlers_;
    }
    if (target == Target::Server) {
        return serverHandlers_;
    }
    if (target == Target::Ui) {
        return uiHandlers_;
    }
    return osHandlers_;
}

const CommandDispatcher::ExampleHandlerMap& CommandDispatcher::getExampleHandlers(
    Target target) const
{
    if (target == Target::Audio) {
        return audioExampleHandlers_;
    }
    if (target == Target::Server) {
        return serverExampleHandlers_;
    }
    if (target == Target::Ui) {
        return uiExampleHandlers_;
    }
    return osExampleHandlers_;
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

    const char* targetLabel = "os-manager";
    if (target == Target::Audio) {
        targetLabel = "audio";
    }
    else if (target == Target::Server) {
        targetLabel = "server";
    }
    else if (target == Target::Ui) {
        targetLabel = "UI";
    }

    spdlog::debug("CommandDispatcher: Dispatching {} command '{}'", targetLabel, commandName);
    return it->second(client, body);
}

bool CommandDispatcher::hasCommand(Target target, const std::string& commandName) const
{
    const auto& handlers = getHandlers(target);
    return handlers.find(commandName) != handlers.end();
}

Result<nlohmann::json, ApiError> CommandDispatcher::getExample(
    Target target, const std::string& commandName) const
{
    const auto& handlers = getExampleHandlers(target);
    auto it = handlers.find(commandName);
    if (it == handlers.end()) {
        return Result<nlohmann::json, ApiError>::error(
            ApiError{ "Unknown command: " + commandName });
    }

    return Result<nlohmann::json, ApiError>::okay(it->second());
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
