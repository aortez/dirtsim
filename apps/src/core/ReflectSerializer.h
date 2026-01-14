#pragma once

#include "reflect.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <type_traits>

/**
 * Generic reflection-based JSON serialization for aggregate types.
 *
 * Uses qlibs/reflect for compile-time introspection and nlohmann/json
 * for JSON generation. Works automatically with any aggregate type.
 *
 * Example:
 *   struct Point { double x = 0.0; double y = 0.0; };
 *   Point p{1.5, 2.5};
 *   auto j = ReflectSerializer::to_json(p);
 *   auto p2 = ReflectSerializer::from_json<Point>(j);
 */
namespace ReflectSerializer {

// Type trait to detect std::optional.
template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};
template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

/**
 * Serialize any aggregate type to nlohmann::json.
 */
template <typename T>
nlohmann::json to_json(const T& obj)
{
    nlohmann::json j;

    // Use qlibs/reflect to iterate over all members.
    reflect::for_each(
        [&](auto I) {
            auto name = std::string(reflect::member_name<I>(obj));
            const auto& value = reflect::get<I>(obj);

            using MemberType = std::remove_cvref_t<decltype(value)>;

            if constexpr (is_optional_v<MemberType>) {
                // Only include optional fields if they have a value.
                if (value.has_value()) {
                    j[name] = *value;
                }
            }
            else if constexpr (std::is_enum_v<MemberType>) {
                // Handle enums directly using reflect.
                j[name] = std::string(reflect::enum_name(value));
            }
            else {
                j[name] = value;
            }
        },
        obj);

    return j;
}

/**
 * Deserialize nlohmann::json to any aggregate type.
 */
template <typename T>
T from_json(const nlohmann::json& j)
{
    T obj{};

    // Use qlibs/reflect to iterate over all members.
    reflect::for_each(
        [&](auto I) {
            auto name = std::string(reflect::member_name<I>(obj));

            using MemberType = std::remove_reference_t<decltype(reflect::get<I>(obj))>;

            if constexpr (is_optional_v<MemberType>) {
                // Optional fields: only set if present in JSON.
                if (j.contains(name) && !j[name].is_null()) {
                    using InnerType = typename MemberType::value_type;
                    reflect::get<I>(obj) = j[name].get<InnerType>();
                }
            }
            else if constexpr (std::is_enum_v<MemberType>) {
                // Handle enums directly using reflect.
                if (j.contains(name)) {
                    auto str = j[name].get<std::string>();
                    bool found = false;
                    for (const auto& [value, enumName] : reflect::enumerators<MemberType>) {
                        if (enumName == str) {
                            reflect::get<I>(obj) = static_cast<MemberType>(value);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        throw std::runtime_error("Invalid enum value: " + str);
                    }
                }
            }
            else {
                // Non-optional fields: set if present.
                if (j.contains(name)) {
                    reflect::get<I>(obj) = j[name].get<MemberType>();
                }
            }
        },
        obj);

    return obj;
}

} // namespace ReflectSerializer
