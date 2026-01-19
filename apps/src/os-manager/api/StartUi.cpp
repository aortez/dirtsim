#include "StartUi.h"

namespace DirtSim {
namespace OsApi {
namespace StartUi {

nlohmann::json Command::toJson() const
{
    return nlohmann::json::object();
}

Command Command::fromJson(const nlohmann::json& /*j*/)
{
    return Command{};
}

} // namespace StartUi
} // namespace OsApi
} // namespace DirtSim
