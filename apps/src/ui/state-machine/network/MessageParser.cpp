#include "MessageParser.h"
#include "core/LoggingChannels.h"
#include "core/PhysicsSettings.h"
#include "core/WorldData.h"

namespace DirtSim {
namespace Ui {

std::optional<Event> MessageParser::parse(const std::string& message)
{
    try {
        nlohmann::json json = nlohmann::json::parse(message);

        // Type 1: Error responses.
        if (json.contains("error")) {
            handleError(json);
            return std::nullopt; // Error responses don't generate events.
        }

        // Type 2: Success responses with data.
        if (json.contains("value")) {
            return parseWorldDataResponse(json);
        }

        LOG_WARN(Network, "Unknown message format: {}", message);
        return std::nullopt;
    }
    catch (const std::exception& e) {
        LOG_ERROR(Network, "Failed to parse message: {}", e.what());
        LOG_DEBUG(Network, "Invalid message: {}", message);
        return std::nullopt;
    }
}

std::optional<Event> MessageParser::parseWorldDataResponse(const nlohmann::json& json)
{
    // All successful responses now include response_type.
    if (!json.contains("response_type")) {
        // Empty responses (monostate) or untyped responses - just log and ignore.
        LOG_DEBUG(Network, "Received response without type: {}", json.dump());
        return std::nullopt;
    }

    std::string responseType = json["response_type"];
    const auto& value = json["value"];

    // Route by explicit response_type.
    if (responseType == "StateGet" || responseType == "state_get") {
        // WorldData response (wrapped in Okay struct).
        WorldData worldData = value["worldData"].get<WorldData>();

        uint64_t stepCount = worldData.timestep;
        UiUpdateEvent evt{ .sequenceNum = 0,
                           .worldData = std::move(worldData),
                           .fps = static_cast<uint32_t>(worldData.fps_server),
                           .stepCount = stepCount,
                           .isPaused = false,
                           .timestamp = std::chrono::steady_clock::now(),
                           .serverSendTimestampNs = 0,
                           .scenario_id = Scenario::EnumType::Empty,
                           .scenario_config = Config::Empty{},
                           .scenarioVideoFrame = std::nullopt };

        return evt;
    }
    else if (responseType == "PhysicsSettingsGet") {
        // PhysicsSettings response (wrapped in Okay struct).
        PhysicsSettings settings = value["settings"].get<PhysicsSettings>();

        LOG_INFO(
            Network,
            "Parsed PhysicsSettings (gravity={:.2f}, hydrostatic={:.2f})",
            settings.gravity,
            settings.pressure_hydrostatic_strength);

        return PhysicsSettingsReceivedEvent{ settings };
    }
    else {
        // Unknown response type - log for debugging.
        LOG_DEBUG(Network, "Unhandled response_type '{}': {}", responseType, value.dump());
        return std::nullopt;
    }
}

void MessageParser::handleError(const nlohmann::json& json)
{
    std::string errorMsg = json["error"].get<std::string>();
    LOG_ERROR(Network, "DSSM error: {}", errorMsg);
    // TODO: Could queue an ErrorEvent here if we add one.
}

} // namespace Ui
} // namespace DirtSim
