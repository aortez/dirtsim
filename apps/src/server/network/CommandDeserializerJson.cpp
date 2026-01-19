#include "CommandDeserializerJson.h"
#include "server/api/CellGet.h"
#include "server/api/CellSet.h"
#include "server/api/ClockEventTrigger.h"
#include "server/api/DiagramGet.h"
#include "server/api/Exit.h"
#include "server/api/FingerDown.h"
#include "server/api/FingerMove.h"
#include "server/api/FingerUp.h"
#include "server/api/GravitySet.h"
#include "server/api/PeersGet.h"
#include "server/api/PerfStatsGet.h"
#include "server/api/PhysicsSettingsGet.h"
#include "server/api/PhysicsSettingsSet.h"
#include "server/api/RenderFormatGet.h"
#include "server/api/RenderFormatSet.h"
#include "server/api/Reset.h"
#include "server/api/ScenarioConfigSet.h"
#include "server/api/SeedAdd.h"
#include "server/api/SimRun.h"
#include "server/api/SimStop.h"
#include "server/api/SpawnDirtBall.h"
#include "server/api/StateGet.h"
#include "server/api/TimerStatsGet.h"
#include "server/api/TrainingResultDiscard.h"
#include "server/api/TrainingResultGet.h"
#include "server/api/TrainingResultList.h"
#include "server/api/TrainingResultSave.h"
#include "server/api/TrainingResultSet.h"
#include <cctype>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Server {

Result<ApiCommand, ApiError> CommandDeserializerJson::deserialize(const std::string& commandJson)
{
    // Parse JSON command.
    nlohmann::json cmd;

    try {
        cmd = nlohmann::json::parse(commandJson);
    }
    catch (const nlohmann::json::parse_error& e) {
        return Result<ApiCommand, ApiError>::error(
            ApiError(std::string("JSON parse error: ") + e.what()));
    }

    if (!cmd.is_object()) {
        return Result<ApiCommand, ApiError>::error(ApiError("Command must be a JSON object"));
    }

    if (!cmd.contains("command") || !cmd["command"].is_string()) {
        return Result<ApiCommand, ApiError>::error(
            ApiError("Command must have 'command' field with string value"));
    }

    const std::string commandName = cmd["command"].get<std::string>();
    spdlog::debug("Deserializing command: {}", commandName);

    // Dispatch to appropriate handler.
    try {
        if (commandName == Api::CellGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::CellGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::CellSet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::CellSet::Command::fromJson(cmd));
        }
        else if (commandName == Api::ClockEventTrigger::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(
                Api::ClockEventTrigger::Command::fromJson(cmd));
        }
        else if (commandName == Api::DiagramGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::DiagramGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::Exit::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::Exit::Command::fromJson(cmd));
        }
        else if (commandName == Api::FingerDown::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::FingerDown::Command::fromJson(cmd));
        }
        else if (commandName == Api::FingerMove::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::FingerMove::Command::fromJson(cmd));
        }
        else if (commandName == Api::FingerUp::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::FingerUp::Command::fromJson(cmd));
        }
        else if (commandName == Api::GravitySet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::GravitySet::Command::fromJson(cmd));
        }
        else if (commandName == Api::PeersGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::PeersGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::PerfStatsGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::PerfStatsGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::PhysicsSettingsGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(
                Api::PhysicsSettingsGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::PhysicsSettingsSet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(
                Api::PhysicsSettingsSet::Command::fromJson(cmd));
        }
        else if (commandName == Api::RenderFormatGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::RenderFormatGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::RenderFormatSet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::RenderFormatSet::Command::fromJson(cmd));
        }
        else if (commandName == Api::Reset::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::Reset::Command::fromJson(cmd));
        }
        else if (commandName == Api::ScenarioConfigSet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(
                Api::ScenarioConfigSet::Command::fromJson(cmd));
        }
        else if (commandName == Api::SeedAdd::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::SeedAdd::Command::fromJson(cmd));
        }
        else if (commandName == Api::SimRun::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::SimRun::Command::fromJson(cmd));
        }
        else if (commandName == Api::SimStop::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::SimStop::Command::fromJson(cmd));
        }
        else if (commandName == Api::SpawnDirtBall::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::SpawnDirtBall::Command::fromJson(cmd));
        }
        else if (commandName == Api::StateGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::StateGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::StatusGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::StatusGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::TimerStatsGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::TimerStatsGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::TrainingResultDiscard::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(
                Api::TrainingResultDiscard::Command::fromJson(cmd));
        }
        else if (commandName == Api::TrainingResultGet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(
                Api::TrainingResultGet::Command::fromJson(cmd));
        }
        else if (commandName == Api::TrainingResultList::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(
                Api::TrainingResultList::Command::fromJson(cmd));
        }
        else if (commandName == Api::TrainingResultSave::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(
                Api::TrainingResultSave::Command::fromJson(cmd));
        }
        else if (commandName == Api::TrainingResultSet::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(
                Api::TrainingResultSet::Command::fromJson(cmd));
        }
        else if (commandName == Api::WorldResize::Command::name()) {
            return Result<ApiCommand, ApiError>::okay(Api::WorldResize::Command::fromJson(cmd));
        }
        // Legacy aliases for backward compatibility.
        else if (commandName == "place_material") {
            return Result<ApiCommand, ApiError>::okay(Api::CellSet::Command::fromJson(cmd));
        }
        else if (commandName == "get_cell") {
            return Result<ApiCommand, ApiError>::okay(Api::CellGet::Command::fromJson(cmd));
        }
        else if (commandName == "get_state") {
            return Result<ApiCommand, ApiError>::okay(Api::StateGet::Command::fromJson(cmd));
        }
        else if (commandName == "set_gravity") {
            return Result<ApiCommand, ApiError>::okay(Api::GravitySet::Command::fromJson(cmd));
        }
        else {
            return Result<ApiCommand, ApiError>::error(ApiError("Unknown command: " + commandName));
        }
    }
    catch (const std::exception& e) {
        return Result<ApiCommand, ApiError>::error(
            ApiError(std::string("Error deserializing command: ") + e.what()));
    }
}

} // namespace Server
} // namespace DirtSim
