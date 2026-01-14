#pragma once

#include "ReflectSerializer.h"
#include "reflect.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <variant>

namespace DirtSim {

/**
 * Generic variant serialization using reflection.
 * Works for any std::variant<T...> where all types are reflectable.
 */
template <typename... Ts>
void to_json(nlohmann::json& j, const std::variant<Ts...>& variant)
{
    std::visit(
        [&j](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            j = ReflectSerializer::to_json(value);
            j["_variant_type"] = std::string(reflect::type_name<T>());
        },
        variant);
}

template <typename... Ts>
void from_json(const nlohmann::json& j, std::variant<Ts...>& variant)
{
    std::string type = j.at("_variant_type").get<std::string>();
    bool found = false;

    ((reflect::type_name<Ts>() == type
          ? (variant = ReflectSerializer::from_json<Ts>(j), found = true)
          : false)
     || ...);

    if (!found) {
        throw std::runtime_error("Unknown variant type: " + type);
    }
}

} // namespace DirtSim

namespace nlohmann {

template <typename... Ts>
struct adl_serializer<std::variant<Ts...>> {
    static void to_json(json& j, const std::variant<Ts...>& v) { DirtSim::to_json(j, v); }

    static void from_json(const json& j, std::variant<Ts...>& v) { DirtSim::from_json(j, v); }
};

} // namespace nlohmann
