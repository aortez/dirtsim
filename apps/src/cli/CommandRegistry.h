#pragma once

#include "audio/api/AudioApiCommand.h"
#include "os-manager/api/OsApiCommand.h"
#include "server/api/ApiCommand.h"
#include "ui/state-machine/api/UiApiCommand.h"
#include <array>
#include <string_view>
#include <variant>

namespace DirtSim {
namespace Client {

/**
 * @brief Extract command names from a variant type at compile-time.
 *
 * Uses template parameter pack expansion to call the static name() method
 * on each command type in the variant.
 */
template <typename... CommandTypes>
constexpr auto extractCommandNames(std::variant<CommandTypes...>*)
{
    return std::array<std::string_view, sizeof...(CommandTypes)>{ CommandTypes::name()... };
}

/**
 * @brief Compile-time array of all server API command names.
 *
 * Automatically extracted from the ApiCommand variant.
 * This ensures the CLI registry stays in sync with server commands.
 */
inline constexpr auto SERVER_COMMAND_NAMES = extractCommandNames(static_cast<ApiCommand*>(nullptr));

/**
 * @brief Compile-time array of all UI API command names.
 *
 * Automatically extracted from the UiApiCommand variant.
 * This ensures the CLI registry stays in sync with UI commands.
 */
inline constexpr auto UI_COMMAND_NAMES =
    extractCommandNames(static_cast<Ui::UiApiCommand*>(nullptr));

/**
 * @brief Compile-time array of all OS manager API command names.
 */
inline constexpr auto OS_COMMAND_NAMES =
    extractCommandNames(static_cast<OsApi::OsApiCommand*>(nullptr));

/**
 * @brief Compile-time array of all audio API command names.
 */
inline constexpr auto AUDIO_COMMAND_NAMES =
    extractCommandNames(static_cast<AudioApi::AudioApiCommand*>(nullptr));

/**
 * @brief Check if a command name is a valid server command.
 */
inline bool isServerCommand(std::string_view name)
{
    for (const auto& cmd : SERVER_COMMAND_NAMES) {
        if (cmd == name) return true;
    }
    return false;
}

/**
 * @brief Check if a command name is a valid UI command.
 */
inline bool isUiCommand(std::string_view name)
{
    for (const auto& cmd : UI_COMMAND_NAMES) {
        if (cmd == name) return true;
    }
    return false;
}

/**
 * @brief Check if a command name is a valid OS manager command.
 */
inline bool isOsCommand(std::string_view name)
{
    for (const auto& cmd : OS_COMMAND_NAMES) {
        if (cmd == name) return true;
    }
    return false;
}

/**
 * @brief Check if a command name is a valid audio command.
 */
inline bool isAudioCommand(std::string_view name)
{
    for (const auto& cmd : AUDIO_COMMAND_NAMES) {
        if (cmd == name) return true;
    }
    return false;
}

} // namespace Client
} // namespace DirtSim
