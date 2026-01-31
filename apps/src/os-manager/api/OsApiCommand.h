#pragma once

#include "Reboot.h"
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
#include "WebSocketAccessSet.h"
#include "WebUiAccessSet.h"
#include <variant>

namespace DirtSim {
namespace OsApi {

using OsApiCommand = std::variant<
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
    WebSocketAccessSet::Command,
    WebUiAccessSet::Command>;

} // namespace OsApi
} // namespace DirtSim
