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
    if (str == "DuckTraining") {
        return EnumType::Clock;
    }

    for (const auto& [value, name] : reflect::enumerators<EnumType>) {
        if (name == str) {
            return static_cast<EnumType>(value);
        }
    }
    return std::nullopt;
}

void to_json(nlohmann::json& j, const EnumType& id)
{
    if (id == EnumType::DuckTraining) {
        j = static_cast<int>(EnumType::Clock);
        return;
    }

    j = static_cast<int>(id);
}

void from_json(const nlohmann::json& j, EnumType& id)
{
    if (!j.is_number_integer()) {
        throw std::runtime_error("Scenario id must be an integer.");
    }

    const int value = j.get<int>();
    if (value == static_cast<int>(EnumType::DuckTraining)) {
        id = EnumType::Clock;
        return;
    }

    for (const auto& enumerator : reflect::enumerators<EnumType>) {
        if (static_cast<int>(enumerator.first) == value) {
            id = static_cast<EnumType>(enumerator.first);
            return;
        }
    }

    throw std::runtime_error("Invalid scenario id: " + std::to_string(value));
}

} // namespace DirtSim::Scenario
