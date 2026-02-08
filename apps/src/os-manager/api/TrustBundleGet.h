#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "os-manager/PeerTrust.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace TrustBundleGet {

DEFINE_API_NAME(TrustBundleGet);

struct Okay;

struct Command {
    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    OsManager::PeerTrustBundle bundle;
    bool client_key_created = false;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace TrustBundleGet
} // namespace OsApi
} // namespace DirtSim
