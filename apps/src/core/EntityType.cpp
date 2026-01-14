#include "EntityType.h"
#include "reflect.h"

namespace DirtSim {

std::string toString(EntityType type)
{
    return std::string(reflect::enum_name(type));
}

std::optional<EntityType> fromString(const std::string& str)
{
    for (const auto& [value, name] : reflect::enumerators<EntityType>) {
        if (name == str) {
            return static_cast<EntityType>(value);
        }
    }
    return std::nullopt;
}

} // namespace DirtSim
