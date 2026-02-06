#pragma once

#include "PeerClientKeyEnsure.h"
#include "PeersGet.h"
#include "Reboot.h"
#include "RemoteCliRun.h"
#include "RestartAudio.h"
#include "RestartServer.h"
#include "RestartUi.h"
#include "StartAudio.h"
#include "StartServer.h"
#include "StartUi.h"
#include "StopAudio.h"
#include "StopServer.h"
#include "StopUi.h"
#include "SystemStatus.h"
#include "TrustBundleGet.h"
#include "TrustPeer.h"
#include "UntrustPeer.h"
#include "WebSocketAccessSet.h"
#include "WebUiAccessSet.h"
#include <variant>

namespace DirtSim {
namespace OsApi {

using OsApiCommand = std::variant<
    PeerClientKeyEnsure::Command,
    PeersGet::Command,
    RemoteCliRun::Command,
    Reboot::Command,
    RestartAudio::Command,
    RestartServer::Command,
    RestartUi::Command,
    StartAudio::Command,
    StartServer::Command,
    StartUi::Command,
    StopAudio::Command,
    StopServer::Command,
    StopUi::Command,
    SystemStatus::Command,
    TrustBundleGet::Command,
    TrustPeer::Command,
    UntrustPeer::Command,
    WebSocketAccessSet::Command,
    WebUiAccessSet::Command>;

} // namespace OsApi
} // namespace DirtSim
