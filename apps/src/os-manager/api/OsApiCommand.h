#pragma once

#include "Reboot.h"
#include "RestartServer.h"
#include "RestartUi.h"
#include "StartServer.h"
#include "StartUi.h"
#include "StopServer.h"
#include "StopUi.h"
#include "SystemStatus.h"
#include <variant>

namespace DirtSim {
namespace OsApi {

using OsApiCommand = std::variant<
    Reboot::Command,
    RestartServer::Command,
    RestartUi::Command,
    StartServer::Command,
    StartUi::Command,
    StopServer::Command,
    StopUi::Command,
    SystemStatus::Command>;

} // namespace OsApi
} // namespace DirtSim
