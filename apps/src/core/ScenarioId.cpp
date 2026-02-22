#include "ScenarioId.h"
#include "reflect.h"

#include <stdexcept>

namespace DirtSim::Scenario {

std::string toString(EnumType id)
{
    return std::string(reflect::enum_name(id));
}

std::optional<EnumType> fromString(const std::string& str)
{
    for (const auto& [value, name] : reflect::enumerators<EnumType>) {
        if (name == str) {
            return static_cast<EnumType>(value);
        }
    }
    return std::nullopt;
}

void to_json(nlohmann::json& j, const EnumType& id)
{
    j = toString(id);
}

void from_json(const nlohmann::json& j, EnumType& id)
{
    const auto result = fromString(j.get<std::string>());
    if (!result.has_value()) {
        throw std::runtime_error("Unknown scenario id: " + j.get<std::string>());
    }
    id = result.value();
}

} // namespace DirtSim::Scenario
