#include "CommandDeserializerJson.h"
#include "os-manager/api/Reboot.h"
#include "os-manager/api/RestartAudio.h"
#include "os-manager/api/RestartServer.h"
#include "os-manager/api/RestartUi.h"
#include "os-manager/api/StartAudio.h"
#include "os-manager/api/StartServer.h"
#include "os-manager/api/StartUi.h"
#include "os-manager/api/StopAudio.h"
#include "os-manager/api/StopServer.h"
#include "os-manager/api/StopUi.h"
#include "os-manager/api/SystemStatus.h"
#include "os-manager/api/WebSocketAccessSet.h"
#include "os-manager/api/WebUiAccessSet.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace OsManager {

Result<OsApi::OsApiCommand, ApiError> CommandDeserializerJson::deserialize(
    const std::string& commandJson)
{
    nlohmann::json cmd;

    try {
        cmd = nlohmann::json::parse(commandJson);
    }
    catch (const nlohmann::json::parse_error& e) {
        return Result<OsApi::OsApiCommand, ApiError>::error(
            ApiError(std::string("JSON parse error: ") + e.what()));
    }

    if (!cmd.is_object()) {
        return Result<OsApi::OsApiCommand, ApiError>::error(
            ApiError("Command must be a JSON object"));
    }

    if (!cmd.contains("command") || !cmd["command"].is_string()) {
        return Result<OsApi::OsApiCommand, ApiError>::error(
            ApiError("Command must have 'command' field with string value"));
    }

    const std::string commandName = cmd["command"].get<std::string>();
    spdlog::debug("OsManager: Deserializing command: {}", commandName);

    try {
        if (commandName == OsApi::Reboot::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::Reboot::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::RestartAudio::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::RestartAudio::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::RestartServer::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::RestartServer::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::RestartUi::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::RestartUi::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::StartAudio::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::StartAudio::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::StartServer::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::StartServer::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::StartUi::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::StartUi::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::StopAudio::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::StopAudio::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::StopServer::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::StopServer::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::StopUi::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::StopUi::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::SystemStatus::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::SystemStatus::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::WebSocketAccessSet::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::WebSocketAccessSet::Command::fromJson(cmd));
        }
        else if (commandName == OsApi::WebUiAccessSet::Command::name()) {
            return Result<OsApi::OsApiCommand, ApiError>::okay(
                OsApi::WebUiAccessSet::Command::fromJson(cmd));
        }
        else {
            return Result<OsApi::OsApiCommand, ApiError>::error(
                ApiError("Unknown command: " + commandName));
        }
    }
    catch (const std::exception& e) {
        return Result<OsApi::OsApiCommand, ApiError>::error(
            ApiError(std::string("Error deserializing command: ") + e.what()));
    }
}

} // namespace OsManager
} // namespace DirtSim
