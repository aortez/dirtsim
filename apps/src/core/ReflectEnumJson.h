#pragma once

// Automatic enum JSON serialization using qlibs/reflect.
//
// Usage:
//   #include "ReflectEnumJson.h"
//
//   enum class MyEnum : uint8_t { Foo = 0, Bar = 1, Baz = 2 };
//
//   nlohmann::json j = MyEnum::Bar;  // "Bar"
//   MyEnum e = j.get<MyEnum>();      // MyEnum::Bar
//
// Requirements:
//   - Enum values must be sequential starting from 0, or you must specialize
//     reflect::enum_min/enum_max for your enum type.

#include "reflect.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace nlohmann {

// Specialize adl_serializer for all enum types.
template <typename E>
    requires std::is_enum_v<E>
struct adl_serializer<E> {
    static void to_json(json& j, E e) { j = std::string(reflect::enum_name(e)); }

    static void from_json(const json& j, E& e)
    {
        auto name = j.get<std::string>();
        for (const auto& [value, str] : reflect::enumerators<E>) {
            if (str == name) {
                e = static_cast<E>(value);
                return;
            }
        }
        throw std::runtime_error("Invalid enum value: " + name);
    }
};

} // namespace nlohmann
