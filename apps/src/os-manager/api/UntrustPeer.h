#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace UntrustPeer {

DEFINE_API_NAME(UntrustPeer);

struct Okay;

struct Command {
    std::string host;

    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool allowlist_removed = false;
    bool authorized_key_removed = false;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace UntrustPeer
} // namespace OsApi
} // namespace DirtSim
