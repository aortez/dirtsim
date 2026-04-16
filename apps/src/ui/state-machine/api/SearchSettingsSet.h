#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/UserSettings.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {
namespace SearchSettingsSet {

DEFINE_API_NAME(SearchSettingsSet);

struct Okay;

struct Command {
    SearchSettings settings;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    SearchSettings settings;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

using Response = Result<Okay, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace SearchSettingsSet
} // namespace UiApi
} // namespace DirtSim
