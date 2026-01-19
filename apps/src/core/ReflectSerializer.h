#pragma once

#include "reflect.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace ReflectSerializerAdl {

struct JsonAdapter {
    nlohmann::json& json;
    operator nlohmann::json&() const { return json; }
};

struct ConstJsonAdapter {
    const nlohmann::json& json;
    operator const nlohmann::json&() const { return json; }
};

template <typename T>
auto test_to_json(int)
    -> decltype(to_json(JsonAdapter{ std::declval<nlohmann::json&>() }, std::declval<const T&>()), std::true_type{});

template <typename T>
std::false_type test_to_json(...);

template <typename T>
inline constexpr bool has_adl_to_json_v = decltype(test_to_json<T>(0))::value;

template <typename T>
auto test_from_json(int)
    -> decltype(from_json(ConstJsonAdapter{ std::declval<const nlohmann::json&>() }, std::declval<T&>()), std::true_type{});

template <typename T>
std::false_type test_from_json(...);

template <typename T>
inline constexpr bool has_adl_from_json_v = decltype(test_from_json<T>(0))::value;

template <typename T>
void call_to_json(nlohmann::json& j, const T& value)
{
    to_json(JsonAdapter{ j }, value);
}

template <typename T>
void call_from_json(const nlohmann::json& j, T& value)
{
    from_json(ConstJsonAdapter{ j }, value);
}

} // namespace ReflectSerializerAdl

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
    const auto writeEnum = [&j](const std::string& name, const auto& value) {
        using EnumType = std::remove_cvref_t<decltype(value)>;
        if constexpr (ReflectSerializerAdl::has_adl_to_json_v<EnumType>) {
            nlohmann::json enumJson;
            ReflectSerializerAdl::call_to_json(enumJson, value);
            j[name] = std::move(enumJson);
        }
        else {
            j[name] = std::string(reflect::enum_name(value));
        }
    };

    // Use qlibs/reflect to iterate over all members.
    reflect::for_each(
        [&](auto I) {
            auto name = std::string(reflect::member_name<I>(obj));
            const auto& value = reflect::get<I>(obj);

            using MemberType = std::remove_cvref_t<decltype(value)>;

            if constexpr (is_optional_v<MemberType>) {
                // Only include optional fields if they have a value.
                if (value.has_value()) {
                    using InnerType = typename MemberType::value_type;
                    if constexpr (std::is_enum_v<InnerType>) {
                        writeEnum(name, *value);
                    }
                    else {
                        j[name] = *value;
                    }
                }
            }
            else if constexpr (std::is_enum_v<MemberType>) {
                // Handle enums directly using reflect.
                writeEnum(name, value);
            }
            else {
                j[name] = value;
            }
        },
        obj);

    return j;
}

/**
 * Serialize any aggregate type to nlohmann::json, including empty optionals as null.
 */
template <typename T>
nlohmann::json to_json_with_null_optionals(const T& obj)
{
    nlohmann::json j;
    const auto writeEnum = [&j](const std::string& name, const auto& value) {
        using EnumType = std::remove_cvref_t<decltype(value)>;
        if constexpr (ReflectSerializerAdl::has_adl_to_json_v<EnumType>) {
            nlohmann::json enumJson;
            ReflectSerializerAdl::call_to_json(enumJson, value);
            j[name] = std::move(enumJson);
        }
        else {
            j[name] = std::string(reflect::enum_name(value));
        }
    };

    // Use qlibs/reflect to iterate over all members.
    reflect::for_each(
        [&](auto I) {
            auto name = std::string(reflect::member_name<I>(obj));
            const auto& value = reflect::get<I>(obj);

            using MemberType = std::remove_cvref_t<decltype(value)>;

            if constexpr (is_optional_v<MemberType>) {
                if (value.has_value()) {
                    using InnerType = typename MemberType::value_type;
                    if constexpr (std::is_enum_v<InnerType>) {
                        writeEnum(name, *value);
                    }
                    else {
                        j[name] = *value;
                    }
                }
                else {
                    j[name] = nullptr;
                }
            }
            else if constexpr (std::is_enum_v<MemberType>) {
                // Handle enums directly using reflect.
                writeEnum(name, value);
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
    const auto readEnum = [&j](const std::string& name, auto& value) {
        using EnumType = std::remove_cvref_t<decltype(value)>;
        if constexpr (ReflectSerializerAdl::has_adl_from_json_v<EnumType>) {
            ReflectSerializerAdl::call_from_json(j.at(name), value);
        }
        else {
            auto str = j.at(name).get<std::string>();
            bool found = false;
            for (const auto& [enumValue, enumName] : reflect::enumerators<EnumType>) {
                if (enumName == str) {
                    value = static_cast<EnumType>(enumValue);
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw std::runtime_error("Invalid enum value: " + str);
            }
        }
    };

    // Use qlibs/reflect to iterate over all members.
    reflect::for_each(
        [&](auto I) {
            auto name = std::string(reflect::member_name<I>(obj));

            using MemberType = std::remove_reference_t<decltype(reflect::get<I>(obj))>;

            if constexpr (is_optional_v<MemberType>) {
                // Optional fields: only set if present in JSON.
                if (j.contains(name) && !j[name].is_null()) {
                    using InnerType = typename MemberType::value_type;
                    if constexpr (std::is_enum_v<InnerType>) {
                        InnerType enumValue{};
                        readEnum(name, enumValue);
                        reflect::get<I>(obj) = enumValue;
                    }
                    else {
                        reflect::get<I>(obj) = j[name].get<InnerType>();
                    }
                }
            }
            else if constexpr (std::is_enum_v<MemberType>) {
                // Handle enums directly using reflect.
                if (j.contains(name)) {
                    readEnum(name, reflect::get<I>(obj));
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
