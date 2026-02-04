#include "State.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/RenderMessage.h"
#include "core/RenderMessageFull.h"
#include "core/RenderMessageUtils.h"
#include "core/ScenarioConfig.h"
#include "core/WorldData.h"
#include "core/api/UiUpdateEvent.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/ClientHello.h"
#include "core/network/WebSocketService.h"
#include "server/api/EvolutionProgress.h"
#include "server/api/TrainingBestSnapshot.h"
#include "ui/UiComponentManager.h"
#include "ui/controls/LogPanel.h"
#include "ui/state-machine/StateMachine.h"
#include "ui/state-machine/api/DrawDebugToggle.h"
#include "ui/state-machine/network/MessageParser.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <lvgl/lvgl.h>
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace Ui {
namespace State {

namespace {

constexpr int ICON_RAIL_WIDTH = 80;
constexpr int STATUS_HEIGHT = 60;
constexpr uint32_t BG_COLOR = 0x202020;
constexpr uint32_t RAIL_COLOR = 0x303030;

} // namespace

void Disconnected::onEnter(StateMachine& sm)
{
    sm_ = &sm;
    LOG_INFO(
        State,
        "Entered Disconnected state (retry_count={}, retry_pending={})",
        retry_count_,
        retry_pending_);

    createDiagnosticsScreen(sm);
}

void Disconnected::onExit(StateMachine& sm)
{
    LOG_INFO(State, "Exiting Disconnected state");

    logPanel_.reset();

    // Clear the container.
    auto* uiManager = sm.getUiComponentManager();
    if (uiManager) {
        uiManager->clearCurrentContainer();
    }

    mainContainer_ = nullptr;
    iconRail_ = nullptr;
    logButton_ = nullptr;
    contentArea_ = nullptr;
    statusLabel_ = nullptr;
}

void Disconnected::createDiagnosticsScreen(StateMachine& sm)
{
    auto* uiManager = sm.getUiComponentManager();
    if (!uiManager) {
        return;
    }

    // Get the config container (dedicated screen for diagnostics).
    lv_obj_t* screen = uiManager->getDisconnectedDiagnosticsContainer();
    if (!screen) {
        return;
    }

    // Create main container with horizontal flex layout.
    mainContainer_ = lv_obj_create(screen);
    lv_obj_set_size(mainContainer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(mainContainer_, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(mainContainer_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mainContainer_, 0, 0);
    lv_obj_set_style_pad_all(mainContainer_, 0, 0);
    lv_obj_set_flex_flow(mainContainer_, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(mainContainer_, LV_OBJ_FLAG_SCROLLABLE);

    // Create icon rail on the left.
    iconRail_ = lv_obj_create(mainContainer_);
    lv_obj_set_size(iconRail_, ICON_RAIL_WIDTH, LV_PCT(100));
    lv_obj_set_style_bg_color(iconRail_, lv_color_hex(RAIL_COLOR), 0);
    lv_obj_set_style_bg_opa(iconRail_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(iconRail_, 0, 0);
    lv_obj_set_style_pad_all(iconRail_, 4, 0);
    lv_obj_set_flex_flow(iconRail_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        iconRail_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(iconRail_, LV_OBJ_FLAG_SCROLLABLE);

    // Create log icon button.
    logButton_ = LVGLBuilder::actionButton(iconRail_)
                     .icon(LV_SYMBOL_LIST)
                     .mode(LVGLBuilder::ActionMode::Toggle)
                     .size(ICON_RAIL_WIDTH - 8)
                     .checked(true)
                     .glowColor(0x00aaff)
                     .textColor(0x00aaff)
                     .buildOrLog();

    // Create content area (right side).
    contentArea_ = lv_obj_create(mainContainer_);
    lv_obj_set_flex_grow(contentArea_, 1);
    lv_obj_set_height(contentArea_, LV_PCT(100));
    lv_obj_set_style_bg_color(contentArea_, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(contentArea_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(contentArea_, 0, 0);
    lv_obj_set_style_pad_all(contentArea_, 8, 0);
    lv_obj_set_flex_flow(contentArea_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(contentArea_, LV_OBJ_FLAG_SCROLLABLE);

    // Create status label at top of content area.
    statusLabel_ = lv_label_create(contentArea_);
    lv_obj_set_width(statusLabel_, LV_PCT(100));
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0xff6600), 0);
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_bottom(statusLabel_, 8, 0);
    updateStatusLabel();

    // Create log panel container.
    lv_obj_t* logContainer = lv_obj_create(contentArea_);
    lv_obj_set_flex_grow(logContainer, 1);
    lv_obj_set_width(logContainer, LV_PCT(100));
    lv_obj_set_style_bg_opa(logContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logContainer, 0, 0);
    lv_obj_set_style_pad_all(logContainer, 0, 0);
    lv_obj_clear_flag(logContainer, LV_OBJ_FLAG_SCROLLABLE);

    // Create log panel.
    logPanel_ = std::make_unique<LogPanel>(logContainer, "dirtsim.log", 30);
    logPanel_->setRefreshInterval(2.0);

    LOG_INFO(State, "Diagnostics screen created");
}

void Disconnected::updateStatusLabel()
{
    if (!statusLabel_) {
        return;
    }

    std::string status;
    if (retry_pending_) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_attempt_time_).count();
        double remaining = RETRY_INTERVAL_SECONDS - elapsed;
        if (remaining < 0) {
            remaining = 0;
        }

        status = "Unable to connect to server\n";
        status +=
            "Retry " + std::to_string(retry_count_) + "/" + std::to_string(MAX_RETRY_ATTEMPTS);
        status += " in " + std::to_string(static_cast<int>(remaining + 0.5)) + "s...";
    }
    else if (retry_count_ >= MAX_RETRY_ATTEMPTS) {
        status = "Connection failed after " + std::to_string(MAX_RETRY_ATTEMPTS) + " attempts\n";
        status += "Check server status and restart";
    }
    else {
        status = "Connecting to server...";
    }

    lv_label_set_text(statusLabel_, status.c_str());
}

void Disconnected::updateAnimations()
{
    updateStatusLabel();

    if (!retry_pending_ || !sm_) {
        return;
    }

    // Check if enough time has passed since last attempt.
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_attempt_time_).count();

    if (elapsed >= RETRY_INTERVAL_SECONDS) {
        if (retry_count_ >= MAX_RETRY_ATTEMPTS) {
            LOG_ERROR(
                State, "Connection failed after {} retry attempts, giving up", MAX_RETRY_ATTEMPTS);
            retry_pending_ = false;
            return;
        }

        LOG_INFO(
            State,
            "Retrying connection to {}:{} (attempt {}/{})",
            pending_host_,
            pending_port_,
            retry_count_ + 1,
            MAX_RETRY_ATTEMPTS);

        // Queue a new connection attempt.
        sm_->queueEvent(ConnectToServerCommand{ pending_host_, pending_port_ });
    }
}

State::Any Disconnected::onEvent(const ConnectToServerCommand& cmd, StateMachine& sm)
{
    LOG_INFO(State, "Connect command received (host={}, port={})", cmd.host, cmd.port);
    sm.setLastServerAddress(cmd.host, cmd.port);

    auto& wsService = sm.getWebSocketService();

    // Setup callbacks before connecting.
    wsService.onConnected([&sm]() {
        LOG_INFO(Network, "Connected to server");
        sm.queueEvent(ServerConnectedEvent{});
    });

    wsService.onDisconnected([&sm]() {
        LOG_WARN(Network, "Disconnected from server");
        sm.queueEvent(ServerDisconnectedEvent{ "Connection closed" });
    });

    wsService.onError([&sm](const std::string& error) {
        LOG_ERROR(Network, "Connection error: {}", error);
        sm.queueEvent(ServerDisconnectedEvent{ error });
    });

    DirtSim::Network::ClientHello hello{
        .protocolVersion = DirtSim::Network::kClientHelloProtocolVersion,
        .wantsRender = true,
        .wantsEvents = true,
    };
    wsService.setClientHello(hello);

    // Setup callback for server-pushed commands (e.g., DrawDebugToggle from gamepad).
    wsService.onServerCommand(
        [&sm](const std::string& messageType, const std::vector<std::byte>& payload) {
            if (messageType == "DrawDebugToggle") {
                LOG_INFO(Network, "Received DrawDebugToggle command from server");
                UiApi::DrawDebugToggle::Cwc cwc;
                sm.queueEvent(std::move(cwc));
            }
            else if (messageType == "EvolutionProgress") {
                // Deserialize evolution progress broadcast.
                try {
                    auto progress =
                        DirtSim::Network::deserialize_payload<Api::EvolutionProgress>(payload);
                    LOG_DEBUG(
                        Network,
                        "Received EvolutionProgress: gen {}/{}, eval {}/{}",
                        progress.generation,
                        progress.maxGenerations,
                        progress.currentEval,
                        progress.populationSize);
                    sm.queueEvent(EvolutionProgressReceivedEvent{ progress });
                }
                catch (const std::exception& e) {
                    LOG_ERROR(Network, "Failed to deserialize EvolutionProgress: {}", e.what());
                }
            }
            else if (messageType == Api::TrainingBestSnapshot::name()) {
                try {
                    auto snapshot =
                        DirtSim::Network::deserialize_payload<Api::TrainingBestSnapshot>(payload);
                    sm.queueEvent(TrainingBestSnapshotReceivedEvent{ std::move(snapshot) });
                }
                catch (const std::exception& e) {
                    LOG_ERROR(Network, "Failed to deserialize TrainingBestSnapshot: {}", e.what());
                }
            }
            else {
                LOG_WARN(Network, "Unknown server command: {}", messageType);
            }
        });

    // Setup binary callback for RenderMessage pushes from server.
    wsService.onBinary([&sm](const std::vector<std::byte>& bytes) {
        LOG_DEBUG(Network, "Received binary message ({} bytes)", bytes.size());

        try {
            // Deserialize as RenderMessageFull (includes scenario metadata).
            RenderMessageFull fullMsg;
            zpp::bits::in in(bytes);
            in(fullMsg).or_throw();

            const RenderMessage& renderMsg = fullMsg.render_data;

            // Reconstruct WorldData from RenderMessage.
            WorldData worldData;
            worldData.width = renderMsg.width;
            worldData.height = renderMsg.height;
            worldData.timestep = renderMsg.timestep;
            worldData.fps_server = renderMsg.fps_server;

            // Unpack cells based on format.
            size_t numCells = renderMsg.width * renderMsg.height;
            worldData.cells.resize(numCells);
            worldData.colors.resize(renderMsg.width, renderMsg.height);

            if (renderMsg.format == RenderFormat::EnumType::Debug) {
                LOG_DEBUG(Network, "RenderMessage UNPACK: DEBUG format, {} cells", numCells);

                const DebugCell* debugCells =
                    reinterpret_cast<const DebugCell*>(renderMsg.payload.data());

                for (size_t i = 0; i < numCells; ++i) {
                    auto unpacked = RenderMessageUtils::unpackDebugCell(debugCells[i]);
                    worldData.cells[i].material_type = unpacked.material_type;
                    worldData.cells[i].fill_ratio = unpacked.fill_ratio;
                    worldData.cells[i].render_as = unpacked.render_as;
                    worldData.cells[i].com = unpacked.com;
                    worldData.cells[i].velocity = unpacked.velocity;
                    worldData.cells[i].pressure = unpacked.pressure_hydro;
                    worldData.cells[i].pressure_gradient = unpacked.pressure_gradient;
                }
            }
            else {
                // BASIC format: material + fill only.
                LOG_DEBUG(
                    Network,
                    "RenderMessage UNPACK: BASIC format, {} cells (no COM data)",
                    numCells);
                const BasicCell* basicCells =
                    reinterpret_cast<const BasicCell*>(renderMsg.payload.data());
                for (size_t i = 0; i < numCells; ++i) {
                    Material::EnumType material;
                    double fill_ratio;
                    int8_t render_as;
                    uint32_t color;
                    RenderMessageUtils::unpackBasicCell(
                        basicCells[i], material, fill_ratio, render_as, color);
                    worldData.cells[i].material_type = material;
                    worldData.cells[i].fill_ratio = fill_ratio;
                    worldData.cells[i].render_as = render_as;
                    worldData.colors.data[i] = ColorNames::toRgbF(color);
                }
            }

            // Apply sparse organism data.
            worldData.organism_ids =
                RenderMessageUtils::applyOrganismData(renderMsg.organisms, numCells);

            // Copy bone data for structural visualization.
            worldData.bones = renderMsg.bones;

            // Copy tree vision data if present.
            worldData.tree_vision = renderMsg.tree_vision;

            // Copy entities (duck, sparkle, etc.).
            worldData.entities = renderMsg.entities;

            // Create UiUpdateEvent and queue to EventSink.
            auto now = std::chrono::steady_clock::now();
            UiUpdateEvent evt{ .sequenceNum = 0,
                               .worldData = std::move(worldData),
                               .fps = 0,
                               .stepCount = static_cast<uint64_t>(renderMsg.timestep),
                               .isPaused = false,
                               .timestamp = now,
                               .scenario_id = fullMsg.scenario_id,
                               .scenario_config = fullMsg.scenario_config };

            sm.queueEvent(evt);
        }
        catch (const std::exception& e) {
            LOG_ERROR(Network, "Failed to process RenderMessage: {}", e.what());
        }
    });

    // NOW connect (after callbacks are registered).
    std::string url = "ws://" + cmd.host + ":" + std::to_string(cmd.port);
    auto connectResult = wsService.connect(url);
    if (connectResult.isError()) {
        LOG_ERROR(State, "WebSocketService connection failed: {}", connectResult.errorValue());

        // Track retry state - preserve current state but enable retries.
        retry_count_++;
        pending_host_ = cmd.host;
        pending_port_ = cmd.port;
        last_attempt_time_ = std::chrono::steady_clock::now();
        retry_pending_ = true;

        if (retry_count_ < MAX_RETRY_ATTEMPTS) {
            LOG_INFO(
                State,
                "Will retry connection in {:.0f}s (attempt {}/{})",
                RETRY_INTERVAL_SECONDS,
                retry_count_,
                MAX_RETRY_ATTEMPTS);
        }

        updateStatusLabel();
        return std::move(*this);
    }

    // Connection initiated successfully - clear retry state.
    retry_pending_ = false;
    retry_count_ = 0;

    LOG_INFO(State, "WebSocketService connecting to {}", url);

    return StartMenu{};
}

State::Any Disconnected::onEvent(const ServerConnectedEvent& /*evt*/, StateMachine& /*sm*/)
{
    LOG_INFO(State, "Server connection established");

    LOG_INFO(State, "Transitioning to StartMenu");

    return StartMenu{};
}

} // namespace State
} // namespace Ui
} // namespace DirtSim
