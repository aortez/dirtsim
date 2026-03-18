#pragma once

#include <nlohmann/json_fwd.hpp>

namespace DirtSim {

struct UserSettings;

namespace Server {

UserSettings parseUserSettingsDiskJsonWithDefaults(const nlohmann::json& j);

} // namespace Server
} // namespace DirtSim
