#pragma once

#include "core/ReflectSerializer.h"

#include <concepts>
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <variant>

namespace DirtSim {
namespace Network {
namespace JsonProtocolDetail {

template <typename T>
concept HasToJsonMethod = requires(const T& value) {
    { value.toJson() } -> std::convertible_to<nlohmann::json>;
};

template <typename T>
concept HasReflectSerializer = requires(const T& value) {
    { ReflectSerializer::to_json(value) } -> std::convertible_to<nlohmann::json>;
};

template <typename T>
nlohmann::json serializeValue(const T& value)
{
    if constexpr (HasToJsonMethod<T>) {
        return value.toJson();
    }
    else if constexpr (HasReflectSerializer<T>) {
        return ReflectSerializer::to_json(value);
    }
    else {
        return nlohmann::json(value);
    }
}

} // namespace JsonProtocolDetail

inline nlohmann::json makeJsonErrorResponse(uint64_t id, const std::string& message)
{
    return nlohmann::json{ { "id", id }, { "error", message } };
}

template <typename ResponseT>
nlohmann::json makeJsonResponse(uint64_t id, const ResponseT& resp)
{
    nlohmann::json output;
    output["id"] = id;
    if (resp.isError()) {
        output["error"] = resp.errorValue().message;
        return output;
    }

    output["success"] = true;
    using ValueType = std::decay_t<decltype(resp.value())>;
    if constexpr (std::is_same_v<ValueType, std::monostate>) {
        output["value"] = nlohmann::json::object();
    }
    else {
        if constexpr (requires { ValueType::name(); }) {
            output["response_type"] = std::string(ValueType::name());
        }
        output["value"] = JsonProtocolDetail::serializeValue(resp.value());
    }
    return output;
}

} // namespace Network
} // namespace DirtSim
