#include "UserSettingsSet.h"
#include "core/ReflectSerializer.h"
#include <array>
#include <stdexcept>

namespace DirtSim {
namespace Api {
namespace UserSettingsSet {

namespace {

constexpr std::array<const char*, 3> kRequiredSettingsFields = {
    "clockScenarioConfig",
    "volumePercent",
    "defaultScenario",
};

} // namespace

nlohmann::json Command::toJson() const
{
    return ReflectSerializer::to_json(*this);
}

Command Command::fromJson(const nlohmann::json& j)
{
    if (!j.contains("settings") || !j["settings"].is_object()) {
        throw std::runtime_error("settings object is required");
    }

    const auto& settingsJson = j["settings"];
    for (const char* field : kRequiredSettingsFields) {
        if (!settingsJson.contains(field)) {
            throw std::runtime_error(std::string("settings.") + field + " is required");
        }
    }

    return ReflectSerializer::from_json<Command>(j);
}

} // namespace UserSettingsSet
} // namespace Api
} // namespace DirtSim
