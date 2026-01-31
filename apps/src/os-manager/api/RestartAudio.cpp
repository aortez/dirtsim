#include "RestartAudio.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace OsApi {
namespace RestartAudio {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

} // namespace RestartAudio
} // namespace OsApi
} // namespace DirtSim
