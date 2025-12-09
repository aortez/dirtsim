#pragma once

#include "core/ReflectSerializer.h"
#include "core/reflect.h"

namespace DirtSim {

/**
 * @brief Define API name marker type and cached name.
 *
 * Usage: DEFINE_API_NAME(SimRun) at the top of the API namespace.
 * Creates a marker struct and cached api_name using reflect::type_name.
 */
#define DEFINE_API_NAME(Name)                                           \
    struct Name {};                                                     \
    inline static constexpr auto api_name = reflect::type_name<Name>(); \
    static_assert(!api_name.empty(), "API name must not be empty");     \
    static_assert(api_name.size() > 0, "API name extraction failed")

/**
 * @brief Add name() method to Command or Okay structs.
 *
 * Usage: API_COMMAND_NAME() inside Command/Okay struct definitions.
 * Returns the cached api_name from the namespace.
 */
#define API_COMMAND_NAME()                   \
    static constexpr std::string_view name() \
    {                                        \
        return api_name;                     \
    }

/**
 * @brief Add automatic JSON serialization using reflection.
 *
 * Usage: API_JSON_SERIALIZABLE(TypeName) inside Command/Okay struct definitions.
 * Generates toJson() and fromJson() using ReflectSerializer.
 */
#define API_JSON_SERIALIZABLE(TypeName)                   \
    nlohmann::json toJson() const                         \
    {                                                     \
        return ReflectSerializer::to_json(*this);         \
    }                                                     \
    static TypeName fromJson(const nlohmann::json& j)     \
    {                                                     \
        return ReflectSerializer::from_json<TypeName>(j); \
    }

/**
 * @brief Define standard API typedefs at namespace level.
 *
 * Usage: API_STANDARD_TYPES() after Command and Okay struct definitions.
 * Creates OkayType, Response, and Cwc typedefs.
 */
#define API_STANDARD_TYPES()                     \
    using OkayType = Okay;                       \
    using Response = Result<OkayType, ApiError>; \
    using Cwc = CommandWithCallback<Command, Response>;

} // namespace DirtSim
