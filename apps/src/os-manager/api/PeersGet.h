#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "os-manager/network/PeerDiscovery.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace PeersGet {

DEFINE_API_NAME(PeersGet);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    std::vector<OsManager::PeerInfo> peers;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<1>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace PeersGet
} // namespace OsApi
} // namespace DirtSim
