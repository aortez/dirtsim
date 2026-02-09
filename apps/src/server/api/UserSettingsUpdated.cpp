#include "UserSettingsUpdated.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace Api {

nlohmann::json UserSettingsUpdated::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

} // namespace Api
} // namespace DirtSim
