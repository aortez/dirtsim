#include "RenderFormat.h"
#include "reflect.h"

namespace DirtSim::RenderFormat {

std::string toString(EnumType format)
{
    return std::string(reflect::enum_name(format));
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

} // namespace DirtSim::RenderFormat
