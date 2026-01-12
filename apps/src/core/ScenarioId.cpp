#include "ScenarioId.h"
#include "reflect.h"

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

} // namespace DirtSim::Scenario
