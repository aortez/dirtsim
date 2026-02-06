#include "CommandDeserializerJson.h"
#include "audio/api/MasterVolumeSet.h"
#include "audio/api/NoteOff.h"
#include "audio/api/NoteOn.h"
#include "audio/api/StatusGet.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace AudioProcess {

Result<AudioApi::AudioApiCommand, ApiError> CommandDeserializerJson::deserialize(
    const std::string& commandJson)
{
    nlohmann::json cmd;

    try {
        cmd = nlohmann::json::parse(commandJson);
    }
    catch (const nlohmann::json::parse_error& e) {
        return Result<AudioApi::AudioApiCommand, ApiError>::error(
            ApiError(std::string("JSON parse error: ") + e.what()));
    }

    if (!cmd.is_object()) {
        return Result<AudioApi::AudioApiCommand, ApiError>::error(
            ApiError("Command must be a JSON object"));
    }

    if (!cmd.contains("command") || !cmd["command"].is_string()) {
        return Result<AudioApi::AudioApiCommand, ApiError>::error(
            ApiError("Command must have 'command' field with string value"));
    }

    const std::string commandName = cmd["command"].get<std::string>();
    spdlog::debug("Audio: Deserializing command: {}", commandName);

    try {
        if (commandName == AudioApi::NoteOn::Command::name()) {
            return Result<AudioApi::AudioApiCommand, ApiError>::okay(
                AudioApi::NoteOn::Command::fromJson(cmd));
        }
        if (commandName == AudioApi::MasterVolumeSet::Command::name()) {
            return Result<AudioApi::AudioApiCommand, ApiError>::okay(
                AudioApi::MasterVolumeSet::Command::fromJson(cmd));
        }
        if (commandName == AudioApi::NoteOff::Command::name()) {
            return Result<AudioApi::AudioApiCommand, ApiError>::okay(
                AudioApi::NoteOff::Command::fromJson(cmd));
        }
        if (commandName == AudioApi::StatusGet::Command::name()) {
            return Result<AudioApi::AudioApiCommand, ApiError>::okay(
                AudioApi::StatusGet::Command::fromJson(cmd));
        }

        return Result<AudioApi::AudioApiCommand, ApiError>::error(
            ApiError("Unknown command: " + commandName));
    }
    catch (const std::exception& e) {
        return Result<AudioApi::AudioApiCommand, ApiError>::error(
            ApiError(std::string("Error deserializing command: ") + e.what()));
    }
}

} // namespace AudioProcess
} // namespace DirtSim
