#include "CommandDeserializerJson.h"
#include "ui/state-machine/api/DrawDebugToggle.h"
#include "ui/state-machine/api/Exit.h"
#include "ui/state-machine/api/MouseDown.h"
#include "ui/state-machine/api/MouseMove.h"
#include "ui/state-machine/api/MouseUp.h"
#include "ui/state-machine/api/PlantSeed.h"
#include "ui/state-machine/api/RenderModeSelect.h"
#include "ui/state-machine/api/ScreenGrab.h"
#include "ui/state-machine/api/SimPause.h"
#include "ui/state-machine/api/SimRun.h"
#include "ui/state-machine/api/SimStop.h"
#include "ui/state-machine/api/StateGet.h"
#include "ui/state-machine/api/StatusGet.h"
#include "ui/state-machine/api/StreamStart.h"
#include "ui/state-machine/api/TrainingResultDiscard.h"
#include "ui/state-machine/api/TrainingResultSave.h"
#include "ui/state-machine/api/TrainingStart.h"
#include "ui/state-machine/api/WebRtcAnswer.h"
#include "ui/state-machine/api/WebRtcCandidate.h"
#include <cctype>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

Result<UiApiCommand, ApiError> CommandDeserializerJson::deserialize(const std::string& commandJson)
{
    // Parse JSON command.
    nlohmann::json cmd;

    try {
        cmd = nlohmann::json::parse(commandJson);
    }
    catch (const nlohmann::json::parse_error& e) {
        return Result<UiApiCommand, ApiError>::error(
            ApiError(std::string("JSON parse error: ") + e.what()));
    }

    if (!cmd.is_object()) {
        return Result<UiApiCommand, ApiError>::error(ApiError("Command must be a JSON object"));
    }

    if (!cmd.contains("command") || !cmd["command"].is_string()) {
        return Result<UiApiCommand, ApiError>::error(
            ApiError("Command must have 'command' field with string value"));
    }

    std::string commandName = cmd["command"].get<std::string>();
    spdlog::debug("UI: Deserializing command: {}", commandName);

    // Dispatch to appropriate handler.
    try {
        if (commandName == UiApi::DrawDebugToggle::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(
                UiApi::DrawDebugToggle::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::Exit::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::Exit::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::MouseDown::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::MouseDown::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::MouseMove::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::MouseMove::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::MouseUp::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::MouseUp::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::PlantSeed::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::PlantSeed::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::RenderModeSelect::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(
                UiApi::RenderModeSelect::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::ScreenGrab::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::ScreenGrab::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::SimPause::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::SimPause::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::SimRun::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::SimRun::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::SimStop::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::SimStop::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::StateGet::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::StateGet::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::StatusGet::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::StatusGet::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::StreamStart::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(UiApi::StreamStart::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::TrainingResultDiscard::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(
                UiApi::TrainingResultDiscard::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::TrainingResultSave::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(
                UiApi::TrainingResultSave::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::TrainingStart::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(
                UiApi::TrainingStart::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::WebRtcAnswer::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(
                UiApi::WebRtcAnswer::Command::fromJson(cmd));
        }
        else if (commandName == UiApi::WebRtcCandidate::Command::name()) {
            return Result<UiApiCommand, ApiError>::okay(
                UiApi::WebRtcCandidate::Command::fromJson(cmd));
        }
        else {
            return Result<UiApiCommand, ApiError>::error(
                ApiError("Unknown UI command: " + commandName));
        }
    }
    catch (const nlohmann::json::exception& e) {
        return Result<UiApiCommand, ApiError>::error(
            ApiError(std::string("JSON deserialization error: ") + e.what()));
    }
}

} // namespace Ui
} // namespace DirtSim
