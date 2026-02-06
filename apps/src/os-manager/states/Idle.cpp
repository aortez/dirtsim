#include "State.h"
#include "core/LoggingChannels.h"
#include "os-manager/OperatingSystemManager.h"

namespace DirtSim {
namespace OsManager {
namespace State {

namespace {

constexpr const char* kServerService = "dirtsim-server.service";
constexpr const char* kUiService = "dirtsim-ui.service";
constexpr const char* kAudioService = "dirtsim-audio.service";

} // namespace

void Idle::onEnter(OperatingSystemManager& /*osm*/)
{
    LOG_INFO(State, "Idle state ready for commands");
}

void Idle::onExit(OperatingSystemManager& /*osm*/)
{
    LOG_INFO(State, "Exiting Idle state");
}

Any Idle::onEvent(const OsApi::Reboot::Cwc& cwc, OperatingSystemManager& /*osm*/)
{
    LOG_INFO(State, "Reboot command received");
    cwc.sendResponse(OsApi::Reboot::Response::okay(std::monostate{}));
    return Rebooting{};
}

Any Idle::onEvent(const OsApi::PeerClientKeyEnsure::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "PeerClientKeyEnsure command received");
    cwc.sendResponse(osm.ensurePeerClientKey());
    return Idle{};
}

Any Idle::onEvent(const OsApi::PeersGet::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "PeersGet command received");
    OsApi::PeersGet::Okay response;
    response.peers = osm.getPeers();
    cwc.sendResponse(OsApi::PeersGet::Response::okay(std::move(response)));
    return Idle{};
}

Any Idle::onEvent(const OsApi::RemoteCliRun::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "RemoteCliRun command received");
    cwc.sendResponse(osm.remoteCliRun(cwc.command));
    return Idle{};
}

Any Idle::onEvent(const OsApi::RestartAudio::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "RestartAudio command received");
    cwc.sendResponse(osm.restartService(kAudioService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::RestartServer::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "RestartServer command received");
    cwc.sendResponse(osm.restartService(kServerService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::RestartUi::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "RestartUi command received");
    cwc.sendResponse(osm.restartService(kUiService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StartAudio::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StartAudio command received");
    cwc.sendResponse(osm.startService(kAudioService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StartServer::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StartServer command received");
    cwc.sendResponse(osm.startService(kServerService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StartUi::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StartUi command received");
    cwc.sendResponse(osm.startService(kUiService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StopAudio::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StopAudio command received");
    cwc.sendResponse(osm.stopService(kAudioService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StopServer::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StopServer command received");
    cwc.sendResponse(osm.stopService(kServerService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::StopUi::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "StopUi command received");
    cwc.sendResponse(osm.stopService(kUiService));
    return Idle{};
}

Any Idle::onEvent(const OsApi::SystemStatus::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "SystemStatus command received");
    cwc.sendResponse(OsApi::SystemStatus::Response::okay(osm.buildSystemStatus()));
    return Idle{};
}

Any Idle::onEvent(const OsApi::TrustBundleGet::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "TrustBundleGet command received");
    cwc.sendResponse(osm.getTrustBundle());
    return Idle{};
}

Any Idle::onEvent(const OsApi::TrustPeer::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "TrustPeer command received");
    cwc.sendResponse(osm.trustPeer(cwc.command));
    return Idle{};
}

Any Idle::onEvent(const OsApi::UntrustPeer::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "UntrustPeer command received");
    cwc.sendResponse(osm.untrustPeer(cwc.command));
    return Idle{};
}

Any Idle::onEvent(const OsApi::WebSocketAccessSet::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "WebSocketAccessSet command received");
    cwc.sendResponse(osm.setWebSocketAccess(cwc.command.enabled));
    return Idle{};
}

Any Idle::onEvent(const OsApi::WebUiAccessSet::Cwc& cwc, OperatingSystemManager& osm)
{
    LOG_INFO(State, "WebUiAccessSet command received");
    cwc.sendResponse(osm.setWebUiAccess(cwc.command.enabled));
    return Idle{};
}

} // namespace State
} // namespace OsManager
} // namespace DirtSim
