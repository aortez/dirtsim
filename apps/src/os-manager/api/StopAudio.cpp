#include "StopAudio.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace OsApi {
namespace StopAudio {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

} // namespace StopAudio
} // namespace OsApi
} // namespace DirtSim
