#pragma once

#include "server/UserSettings.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

struct UserSettingsUpdated {
    UserSettings settings;

    nlohmann::json toJson() const;
    static constexpr const char* name() { return "UserSettingsUpdated"; }

    using serialize = zpp::bits::members<1>;
};

} // namespace Api
} // namespace DirtSim
