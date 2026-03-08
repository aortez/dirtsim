#include "GenomePoolId.h"
#include "reflect.h"

#include <stdexcept>

namespace DirtSim {

std::string toString(GenomePoolId id)
{
    return std::string(reflect::enum_name(id));
}

void to_json(nlohmann::json& j, const GenomePoolId& id)
{
    j = toString(id);
}

void from_json(const nlohmann::json& j, GenomePoolId& id)
{
    const auto str = j.get<std::string>();
    for (const auto& [value, name] : reflect::enumerators<GenomePoolId>) {
        if (name == str) {
            id = static_cast<GenomePoolId>(value);
            return;
        }
    }
    throw std::runtime_error("Unknown genome pool id: " + str);
}

} // namespace DirtSim
