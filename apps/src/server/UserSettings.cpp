#include "UserSettings.h"
#include "core/ReflectSerializer.h"
#include <stdexcept>

namespace DirtSim {

namespace {

template <typename T>
T fromJsonStrict(const nlohmann::json& j, const char* typeName)
{
    if (!j.is_object()) {
        throw std::runtime_error(std::string(typeName) + " must be a JSON object");
    }

    T obj{};
    reflect::for_each(
        [&](auto I) {
            const auto name = std::string(reflect::member_name<I>(obj));
            using MemberType = std::remove_reference_t<decltype(reflect::get<I>(obj))>;
            if constexpr (!ReflectSerializer::is_optional_v<MemberType>) {
                if (!j.contains(name)) {
                    throw std::runtime_error(
                        std::string(typeName) + " missing required field '" + name + "'");
                }
            }
        },
        obj);

    return ReflectSerializer::from_json<T>(j);
}

void mergeMissingFields(nlohmann::json& target, const nlohmann::json& defaults)
{
    if (!target.is_object() || !defaults.is_object()) {
        return;
    }

    for (const auto& defaultEntry : defaults.items()) {
        if (!target.contains(defaultEntry.key())) {
            target[defaultEntry.key()] = defaultEntry.value();
            continue;
        }

        mergeMissingFields(target[defaultEntry.key()], defaultEntry.value());
    }
}

} // namespace

void from_json(const nlohmann::json& j, UiTrainingConfig& settings)
{
    settings = fromJsonStrict<UiTrainingConfig>(j, "UiTrainingConfig");
}

void to_json(nlohmann::json& j, const UiTrainingConfig& settings)
{
    j = ReflectSerializer::to_json(settings);
}

void from_json(const nlohmann::json& j, NesSessionSettings& settings)
{
    settings = fromJsonStrict<NesSessionSettings>(j, "NesSessionSettings");
}

void to_json(nlohmann::json& j, const NesSessionSettings& settings)
{
    j = ReflectSerializer::to_json(settings);
}

void from_json(const nlohmann::json& j, UserSettings& settings)
{
    if (!j.is_object()) {
        throw std::runtime_error("UserSettings must be a JSON object");
    }

    nlohmann::json merged = j;
    mergeMissingFields(merged, ReflectSerializer::to_json(UserSettings{}));
    settings = fromJsonStrict<UserSettings>(merged, "UserSettings");
}

void to_json(nlohmann::json& j, const UserSettings& settings)
{
    j = ReflectSerializer::to_json(settings);
}

} // namespace DirtSim
