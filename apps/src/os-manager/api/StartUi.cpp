#include "StartUi.h"
#include "core/ReflectSerializer.h"

namespace DirtSim {
namespace OsApi {
namespace StartUi {

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    return ReflectSerializer::from_json<Command>(j);
}

} // namespace StartUi
} // namespace OsApi
} // namespace DirtSim
