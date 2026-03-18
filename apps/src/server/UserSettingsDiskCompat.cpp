#include "UserSettingsDiskCompat.h"
#include "UserSettings.h"
#include "core/ReflectSerializer.h"
#include "core/reflect.h"
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace DirtSim::Server {

namespace {

template <typename T>
struct IsOptional : std::false_type {};

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool kIsOptional = IsOptional<std::remove_cvref_t<T>>::value;

template <typename T>
struct IsVector : std::false_type {};

template <typename T, typename Allocator>
struct IsVector<std::vector<T, Allocator>> : std::true_type {};

template <typename T>
inline constexpr bool kIsVector = IsVector<std::remove_cvref_t<T>>::value;

template <typename T>
using PlainType = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
inline constexpr bool kIsReflectObject = std::is_aggregate_v<PlainType<T>> && !kIsOptional<T>
    && !kIsVector<T> && !std::is_enum_v<PlainType<T>>;

std::string appendIndex(const std::string_view path, const size_t index)
{
    return std::string(path) + "[" + std::to_string(index) + "]";
}

std::string appendPath(const std::string_view path, const std::string_view fieldName)
{
    if (path.empty()) {
        return std::string(fieldName);
    }

    return std::string(path) + "." + std::string(fieldName);
}

template <typename TEnum>
void parseEnum(const nlohmann::json& j, TEnum& value, const std::string_view path)
{
    if constexpr (ReflectSerializerAdl::has_adl_from_json_v<TEnum>) {
        ReflectSerializerAdl::call_from_json(j, value);
        return;
    }

    if (!j.is_string()) {
        throw std::runtime_error(std::string(path) + " must be a JSON string");
    }

    const std::string stringValue = j.get<std::string>();
    for (const auto& [enumValue, enumName] : reflect::enumerators<TEnum>) {
        if (enumName == stringValue) {
            value = static_cast<TEnum>(enumValue);
            return;
        }
    }

    throw std::runtime_error("Invalid enum value '" + stringValue + "' for " + std::string(path));
}

template <typename T>
void normalizeJsonInto(const nlohmann::json& j, T& value, const std::string_view path)
{
    using ValueType = PlainType<T>;

    if constexpr (kIsOptional<ValueType>) {
        using InnerType = typename ValueType::value_type;

        if (j.is_null()) {
            value = std::nullopt;
            return;
        }

        InnerType nestedValue = value.value_or(InnerType{});
        normalizeJsonInto(j, nestedValue, path);
        value = nestedValue;
    }
    else if constexpr (kIsVector<ValueType>) {
        using ItemType = typename ValueType::value_type;

        if (!j.is_array()) {
            throw std::runtime_error(std::string(path) + " must be a JSON array");
        }

        value.clear();
        value.reserve(j.size());
        for (size_t i = 0; i < j.size(); ++i) {
            ItemType item{};
            normalizeJsonInto(j.at(i), item, appendIndex(path, i));
            value.push_back(item);
        }
    }
    else if constexpr (std::is_enum_v<ValueType>) {
        parseEnum(j, value, path);
    }
    else if constexpr (kIsReflectObject<ValueType>) {
        if (!j.is_object()) {
            throw std::runtime_error(std::string(path) + " must be a JSON object");
        }

        reflect::for_each(
            [&](auto I) {
                const auto fieldName = std::string(reflect::member_name<I>(value));
                if (!j.contains(fieldName)) {
                    return;
                }

                normalizeJsonInto(
                    j.at(fieldName), reflect::get<I>(value), appendPath(path, fieldName));
            },
            value);
    }
    else {
        value = j.get<ValueType>();
    }
}

} // namespace

UserSettings parseUserSettingsDiskJsonWithDefaults(const nlohmann::json& j)
{
    UserSettings settings{};
    normalizeJsonInto(j, settings, "UserSettings");
    return settings;
}

} // namespace DirtSim::Server
