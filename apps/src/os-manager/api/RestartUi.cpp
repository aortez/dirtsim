#include "RestartUi.h"

namespace DirtSim {
namespace OsApi {
namespace RestartUi {

nlohmann::json Command::toJson() const
{
    return nlohmann::json::object();
}

Command Command::fromJson(const nlohmann::json& /*j*/)
{
    return Command{};
}

} // namespace RestartUi
} // namespace OsApi
} // namespace DirtSim
