#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "os-manager/ScannerTypes.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace ScannerConfigGet {

DEFINE_API_NAME(ScannerConfigGet);

struct Okay;

struct Command {
    API_COMMAND_T(std::monostate);
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    OsManager::ScannerConfig config = OsManager::scannerDefaultConfig();

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<1>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace ScannerConfigGet
} // namespace OsApi
} // namespace DirtSim
