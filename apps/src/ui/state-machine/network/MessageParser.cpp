#include "MessageParser.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/PhysicsSettings.h"
#include "core/RenderMessageUtils.h"
#include "core/WorldData.h"
#include "core/network/BinaryProtocol.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/PlanPlaybackStopped.h"
#include "server/api/PlanSaved.h"
#include "server/api/SearchProgress.h"
#include "server/api/TrainingBestPlaybackFrame.h"
#include "server/api/TrainingBestSnapshot.h"
#include "server/api/UserSettingsUpdated.h"
#include <zpp_bits.h>

namespace DirtSim {
namespace Ui {

namespace {

size_t checkedRenderCellCount(const RenderMessage& renderMsg, const std::string& context)
{
    DIRTSIM_ASSERT(renderMsg.width >= 0, context + ": width must be non-negative");
    DIRTSIM_ASSERT(renderMsg.height >= 0, context + ": height must be non-negative");
    return static_cast<size_t>(renderMsg.width) * static_cast<size_t>(renderMsg.height);
}

template <typename PayloadT, typename Factory>
std::optional<Event> parseServerPayload(
    const std::vector<std::byte>& payload, const std::string& messageType, const Factory& factory)
{
    try {
        auto parsed = DirtSim::Network::deserialize_payload<PayloadT>(payload);
        return Event(factory(std::move(parsed)));
    }
    catch (const std::exception& e) {
        LOG_ERROR(Network, "Failed to deserialize {}: {}", messageType, e.what());
        return std::nullopt;
    }
}

} // namespace

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

std::optional<Event> MessageParser::parseServerCommand(
    const std::string& messageType, const std::vector<std::byte>& payload)
{
    if (messageType == "DrawDebugToggle") {
        LOG_INFO(Network, "Received DrawDebugToggle command from server");
        return Event(UiApi::DrawDebugToggle::Cwc{});
    }
    if (messageType == Api::EvolutionProgress::name()) {
        return parseServerPayload<Api::EvolutionProgress>(
            payload, messageType, [](Api::EvolutionProgress progress) {
                LOG_DEBUG(
                    Network,
                    "Received EvolutionProgress: gen {}/{}, eval {}/{}",
                    progress.generation,
                    progress.maxGenerations,
                    progress.currentEval,
                    progress.populationSize);
                return EvolutionProgressReceivedEvent{ std::move(progress) };
            });
    }
    if (messageType == Api::SearchProgress::name()) {
        return parseServerPayload<Api::SearchProgress>(
            payload, messageType, [](Api::SearchProgress progress) {
                return SearchProgressReceivedEvent{ std::move(progress) };
            });
    }
    if (messageType == Api::PlanSaved::name()) {
        return parseServerPayload<Api::PlanSaved>(payload, messageType, [](Api::PlanSaved saved) {
            return PlanSavedReceivedEvent{ std::move(saved) };
        });
    }
    if (messageType == Api::PlanPlaybackStopped::name()) {
        return parseServerPayload<Api::PlanPlaybackStopped>(
            payload, messageType, [](Api::PlanPlaybackStopped stopped) {
                return PlanPlaybackStoppedReceivedEvent{ std::move(stopped) };
            });
    }
    if (messageType == Api::TrainingBestSnapshot::name()) {
        return parseServerPayload<Api::TrainingBestSnapshot>(
            payload, messageType, [](Api::TrainingBestSnapshot snapshot) {
                return TrainingBestSnapshotReceivedEvent{ std::move(snapshot) };
            });
    }
    if (messageType == Api::TrainingBestPlaybackFrame::name()) {
        return parseServerPayload<Api::TrainingBestPlaybackFrame>(
            payload, messageType, [](Api::TrainingBestPlaybackFrame frame) {
                return TrainingBestPlaybackFrameReceivedEvent{ std::move(frame) };
            });
    }
    if (messageType == Api::UserSettingsUpdated::name()) {
        return parseServerPayload<Api::UserSettingsUpdated>(
            payload, messageType, [](Api::UserSettingsUpdated settingsUpdate) {
                return UserSettingsUpdatedEvent{ .settings = std::move(settingsUpdate.settings) };
            });
    }

    LOG_WARN(Network, "Unknown server command: {}", messageType);
    return std::nullopt;
}

UiUpdateEvent MessageParser::parseRenderMessage(const std::vector<std::byte>& bytes)
{
    RenderMessageFull fullMsg;
    zpp::bits::in in(bytes);
    in(fullMsg).or_throw();
    return parseRenderMessageFull(fullMsg);
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
    if (responseType == "PhysicsSettingsGet") {
        // PhysicsSettings response (wrapped in Okay struct).
        PhysicsSettings settings = value["settings"].get<PhysicsSettings>();

        LOG_INFO(
            Network,
            "Parsed PhysicsSettings (gravity={:.2f}, hydrostatic={:.2f})",
            settings.gravity,
            settings.pressure_hydrostatic_strength);

        return PhysicsSettingsReceivedEvent{ settings };
    }

    // Unknown response type - log for debugging.
    LOG_DEBUG(Network, "Unhandled response_type '{}': {}", responseType, value.dump());
    return std::nullopt;
}

void MessageParser::handleError(const nlohmann::json& json)
{
    std::string errorMsg = json["error"].get<std::string>();
    LOG_ERROR(Network, "DSSM error: {}", errorMsg);
    // TODO: Could queue an ErrorEvent here if we add one.
}

UiUpdateEvent MessageParser::parseRenderMessageFull(const RenderMessageFull& fullMsg)
{
    const RenderMessage& renderMsg = fullMsg.render_data;
    validateRenderMessage(renderMsg);

    WorldData worldData;
    worldData.width = renderMsg.width;
    worldData.height = renderMsg.height;
    worldData.timestep = renderMsg.timestep;
    worldData.fps_server = renderMsg.fps_server;
    worldData.region_debug_blocks_x = renderMsg.region_blocks_x;
    worldData.region_debug_blocks_y = renderMsg.region_blocks_y;
    worldData.region_debug = renderMsg.region_debug;

    const size_t numCells = checkedRenderCellCount(renderMsg, "parseRenderMessage");
    if (!renderMsg.scenario_video_frame.has_value()) {
        worldData.cells.resize(numCells);
        worldData.colors.resize(renderMsg.width, renderMsg.height);

        if (renderMsg.format == RenderFormat::EnumType::Debug) {
            LOG_DEBUG(Network, "RenderMessage UNPACK: DEBUG format, {} cells", numCells);

            const auto* debugCells = reinterpret_cast<const DebugCell*>(renderMsg.payload.data());

            for (size_t i = 0; i < numCells; ++i) {
                auto unpacked = RenderMessageUtils::unpackDebugCell(debugCells[i]);
                worldData.cells[i].material_type = unpacked.material_type;
                worldData.cells[i].fill_ratio = unpacked.fill_ratio;
                worldData.cells[i].render_as = unpacked.render_as;
                worldData.cells[i].com = unpacked.com;
                worldData.cells[i].velocity = unpacked.velocity;
                worldData.cells[i].static_load = unpacked.pressure_hydro;
                worldData.cells[i].pressure = unpacked.pressure_dynamic;
                worldData.cells[i].pressure_gradient = unpacked.pressure_gradient;
            }
        }
        else if (renderMsg.format == RenderFormat::EnumType::Basic) {
            LOG_DEBUG(
                Network, "RenderMessage UNPACK: BASIC format, {} cells (no COM data)", numCells);

            const auto* basicCells = reinterpret_cast<const BasicCell*>(renderMsg.payload.data());
            for (size_t i = 0; i < numCells; ++i) {
                Material::EnumType material;
                double fillRatio;
                int8_t renderAs;
                uint32_t color;
                RenderMessageUtils::unpackBasicCell(
                    basicCells[i], material, fillRatio, renderAs, color);
                worldData.cells[i].material_type = material;
                worldData.cells[i].fill_ratio = fillRatio;
                worldData.cells[i].render_as = renderAs;
                worldData.colors.data[i] = ColorNames::toRgbF(color);
            }
        }
        else {
            DIRTSIM_ASSERT(false, "parseRenderMessage: unsupported cell render format");
        }
    }

    worldData.organism_ids = RenderMessageUtils::applyOrganismData(renderMsg.organisms, numCells);
    worldData.tree_vision = renderMsg.tree_vision;
    worldData.entities = renderMsg.entities;

    auto now = std::chrono::steady_clock::now();
    return UiUpdateEvent{ .sequenceNum = 0,
                          .worldData = std::move(worldData),
                          .fps = 0,
                          .stepCount = static_cast<uint64_t>(renderMsg.timestep),
                          .isPaused = false,
                          .timestamp = now,
                          .serverSendTimestampNs = fullMsg.server_send_timestamp_ns,
                          .scenario_id = fullMsg.scenario_id,
                          .scenario_config = fullMsg.scenario_config,
                          .nesControllerTelemetry = fullMsg.nes_controller_telemetry,
                          .nesSmbResponseTelemetry = fullMsg.nes_smb_response_telemetry,
                          .scenarioVideoFrame = renderMsg.scenario_video_frame };
}

void MessageParser::validateRenderMessage(const RenderMessage& renderMsg)
{
    if (renderMsg.scenario_video_frame.has_value()) {
        const auto& frame = renderMsg.scenario_video_frame.value();
        DIRTSIM_ASSERT(
            renderMsg.width == static_cast<int16_t>(frame.width),
            "parseRenderMessage: render width must match scenario video frame width");
        DIRTSIM_ASSERT(
            renderMsg.height == static_cast<int16_t>(frame.height),
            "parseRenderMessage: render height must match scenario video frame height");
        DIRTSIM_ASSERT(
            renderMsg.payload.empty(),
            "parseRenderMessage: scenario video frames must not include cell payload");
        const size_t pixelBytes =
            static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * sizeof(uint16_t);
        DIRTSIM_ASSERT(
            frame.pixels.size() == pixelBytes,
            "parseRenderMessage: scenario video frame pixel size must match width * height * 2");
        return;
    }

    const size_t numCells = checkedRenderCellCount(renderMsg, "parseRenderMessage");
    if (renderMsg.format == RenderFormat::EnumType::Debug) {
        DIRTSIM_ASSERT(
            renderMsg.payload.size() == numCells * sizeof(DebugCell),
            "parseRenderMessage: Debug render payload size mismatch");
        return;
    }
    if (renderMsg.format == RenderFormat::EnumType::Basic) {
        DIRTSIM_ASSERT(
            renderMsg.payload.size() == numCells * sizeof(BasicCell),
            "parseRenderMessage: Basic render payload size mismatch");
        return;
    }

    DIRTSIM_ASSERT(false, "parseRenderMessage: unsupported render format");
}

} // namespace Ui
} // namespace DirtSim
