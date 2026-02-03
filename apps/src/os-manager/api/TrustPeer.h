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
namespace TrustPeer {

DEFINE_API_NAME(TrustPeer);

struct Okay;

struct Command {
    OsManager::PeerTrustBundle bundle;

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool allowlist_updated = false;
    bool authorized_key_added = false;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace TrustPeer
} // namespace OsApi
} // namespace DirtSim
