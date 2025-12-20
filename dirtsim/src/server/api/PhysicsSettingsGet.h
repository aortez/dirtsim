#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/PhysicsSettings.h"
#include "core/Result.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {
namespace PhysicsSettingsGet {

DEFINE_API_NAME(PhysicsSettingsGet);

struct Okay;

struct Command {
    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<0>;
    using OkayType = Okay;
};

struct Okay {
    PhysicsSettings settings;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace PhysicsSettingsGet
} // namespace Api
} // namespace DirtSim
