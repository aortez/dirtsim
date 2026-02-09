#include "UserSettings.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {

void from_json(const nlohmann::json& j, UserSettings& settings)
{
    settings = ReflectSerializer::from_json<UserSettings>(j);
}

void to_json(nlohmann::json& j, const UserSettings& settings)
{
    j = ReflectSerializer::to_json(settings);
}

} // namespace DirtSim
