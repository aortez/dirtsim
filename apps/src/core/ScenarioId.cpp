#include "ScenarioId.h"
#include "ReflectEnumJson.h"
#include "reflect.h"

namespace DirtSim {

std::string toString(ScenarioId id)
{
    return std::string(reflect::enum_name(id));
}

std::optional<ScenarioId> fromString(std::string_view str)
{
    for (const auto& [value, name] : reflect::enumerators<ScenarioId>) {
        if (name == str) {
            return static_cast<ScenarioId>(value);
        }
    }
    return std::nullopt;
}

} // namespace DirtSim
