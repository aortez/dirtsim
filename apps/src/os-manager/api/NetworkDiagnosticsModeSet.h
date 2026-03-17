#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace NetworkDiagnosticsModeSet {

DEFINE_API_NAME(NetworkDiagnosticsModeSet);

struct Okay;

struct Command {
    bool active = false;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    bool active = false;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace NetworkDiagnosticsModeSet
} // namespace OsApi
} // namespace DirtSim
