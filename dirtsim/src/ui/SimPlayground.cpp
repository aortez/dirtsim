#include "SimPlayground.h"
#include "controls/CoreControls.h"
#include "controls/ExpandablePanel.h"
#include "controls/PhysicsPanel.h"
#include "controls/SandboxControls.h"
#include "core/LoggingChannels.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "rendering/CellRenderer.h"
#include "rendering/NeuralGridRenderer.h"
#include "server/api/SimRun.h"
#include "state-machine/EventSink.h"
#include "ui/UiComponentManager.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <atomic>
#include <cstring>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

SimPlayground::SimPlayground(
    UiComponentManager* uiManager, Network::WebSocketService* wsService, EventSink& eventSink)
    : uiManager_(uiManager), wsService_(wsService), eventSink_(eventSink)
{
    renderer_ = std::make_unique<CellRenderer>();
    neuralGridRenderer_ = std::make_unique<NeuralGridRenderer>();

    LOG_INFO(Controls, "Initialized");
}

SimPlayground::~SimPlayground()
{
    LOG_INFO(Controls, "Destroyed");
}

void SimPlayground::connectToIconRail()
{
    IconRail* iconRail = uiManager_->getIconRail();
    if (iconRail) {
        iconRail->setSecondaryCallback(
            [this](IconId selectedId, IconId previousId) {
                onIconSelected(selectedId, previousId);
            });
        LOG_INFO(Controls, "Connected to IconRail selection callback");
    }
    else {
        LOG_ERROR(Controls, "No IconRail available to connect to");
    }
}

void SimPlayground::onIconSelected(IconId selectedId, IconId previousId)
{
    LOG_INFO(
        Controls,
        "SimPlayground: Icon selection {} -> {}",
        static_cast<int>(previousId),
        static_cast<int>(selectedId));

    // Tree icon is handled specially by UiComponentManager (toggles neural grid).
    // We don't need to do anything extra here for tree.

    // For other icons, show the appropriate panel content.
    if (selectedId != IconId::COUNT && selectedId != IconId::TREE) {
        showPanelContent(selectedId);
    }
    else if (selectedId == IconId::COUNT) {
        // No icon selected - clear panel.
        clearPanelContent();
    }
}

void SimPlayground::showPanelContent(IconId panelId)
{
    if (panelId == activePanel_) return; // Already showing this panel.

    ExpandablePanel* panel = uiManager_->getExpandablePanel();
    if (!panel) {
        LOG_ERROR(Controls, "No expandable panel available");
        return;
    }

    // Clear existing content.
    clearPanelContent();

    // Get content area.
    lv_obj_t* container = panel->getContentArea();
    if (!container) {
        LOG_ERROR(Controls, "No panel content area available");
        return;
    }

    // Create content for the selected panel.
    switch (panelId) {
    case IconId::CORE:
        createCorePanel(container);
        break;
    case IconId::SCENARIO:
        createScenarioPanel(container);
        break;
    case IconId::PHYSICS:
        createPhysicsPanel(container);
        break;
    default:
        LOG_WARN(Controls, "Unknown panel id: {}", static_cast<int>(panelId));
        return;
    }

    activePanel_ = panelId;
    LOG_DEBUG(Controls, "Showing panel content for icon {}", static_cast<int>(panelId));
}

void SimPlayground::clearPanelContent()
{
    // Reset panel-specific controls.
    coreControls_.reset();
    physicsPanel_.reset();
    sandboxControls_.reset();
    scenarioDropdown_ = nullptr;

    // Clear the panel's content area.
    ExpandablePanel* panel = uiManager_->getExpandablePanel();
    if (panel) {
        panel->clearContent();
    }

    activePanel_ = IconId::COUNT;
}

void SimPlayground::createCorePanel(lv_obj_t* container)
{
    LOG_DEBUG(Controls, "Creating Core panel");

    coreControls_ =
        std::make_unique<CoreControls>(container, wsService_, eventSink_, renderMode_);
}

void SimPlayground::createScenarioPanel(lv_obj_t* container)
{
    LOG_DEBUG(Controls, "Creating Scenario panel");

    // Scenario label.
    lv_obj_t* scenarioLabel = lv_label_create(container);
    lv_label_set_text(scenarioLabel, "Scenario:");
    lv_obj_set_style_text_color(scenarioLabel, lv_color_hex(0xFFFFFF), 0);

    // Scenario dropdown.
    scenarioDropdown_ = LVGLBuilder::dropdown(container)
                            .options("Sandbox\nDam Break\nEmpty\nFalling Dirt\nRaining\nTree "
                                     "Germination\nWater Equalization")
                            .selected(0) // "Sandbox" selected by default.
                            .size(LV_PCT(95), 40)
                            .buildOrLog();

    if (scenarioDropdown_) {
        // Style the dropdown button (light green background, dark purple text).
        lv_obj_set_style_bg_color(
            scenarioDropdown_, lv_color_hex(0x90EE90), LV_PART_MAIN); // Light green.
        lv_obj_set_style_text_color(
            scenarioDropdown_, lv_color_hex(0x4B0082), LV_PART_MAIN); // Dark purple (indigo).

        // Style the dropdown list (when opened).
        lv_obj_set_style_bg_color(
            lv_dropdown_get_list(scenarioDropdown_),
            lv_color_hex(0x90EE90),
            LV_PART_MAIN); // Light green.
        lv_obj_set_style_text_color(
            lv_dropdown_get_list(scenarioDropdown_),
            lv_color_hex(0x4B0082),
            LV_PART_MAIN); // Dark purple.

        lv_obj_set_user_data(scenarioDropdown_, this);
        lv_obj_add_event_cb(scenarioDropdown_, onScenarioChanged, LV_EVENT_VALUE_CHANGED, this);
    }

    // Create sandbox controls if we're already in sandbox scenario.
    if (currentScenarioId_ == "sandbox"
        && std::holds_alternative<SandboxConfig>(currentScenarioConfig_)) {
        const SandboxConfig& config = std::get<SandboxConfig>(currentScenarioConfig_);
        sandboxControls_ = std::make_unique<SandboxControls>(container, wsService_, config);
    }
}

void SimPlayground::createPhysicsPanel(lv_obj_t* container)
{
    LOG_DEBUG(Controls, "Creating Physics panel");

    // Create a title.
    lv_obj_t* title = lv_label_create(container);
    lv_label_set_text(title, "Physics Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    physicsPanel_ = std::make_unique<PhysicsPanel>(container, wsService_);
}

void SimPlayground::updatePhysicsPanels(const PhysicsSettings& settings)
{
    if (physicsPanel_) {
        physicsPanel_->updateFromSettings(settings);
    }
}

void SimPlayground::updateFromWorldData(const WorldData& data, double uiFPS)
{
    // Update stats display if core panel is active.
    if (coreControls_) {
        coreControls_->updateStats(data.fps_server, uiFPS);
    }

    // Track tree existence for icon visibility.
    bool treeNowExists = data.tree_vision.has_value();
    if (treeNowExists != treeExists_) {
        treeExists_ = treeNowExists;

        // Update tree icon visibility.
        IconRail* iconRail = uiManager_->getIconRail();
        if (iconRail) {
            iconRail->setTreeIconVisible(treeExists_);
            LOG_INFO(Controls, "Tree icon visibility: {}", treeExists_);
        }
    }

    // Handle scenario changes.
    if (data.scenario_id != currentScenarioId_) {
        LOG_INFO(Controls, "Scenario changed to '{}'", data.scenario_id);

        // Clear old scenario controls.
        sandboxControls_.reset();

        currentScenarioId_ = data.scenario_id;
    }

    // Store the config for use when panel is opened.
    currentScenarioConfig_ = data.scenario_config;

    // Create sandbox controls if scenario panel is active and we're in sandbox mode.
    if (activePanel_ == IconId::SCENARIO && currentScenarioId_ == "sandbox" && !sandboxControls_) {
        ExpandablePanel* panel = uiManager_->getExpandablePanel();
        if (panel && std::holds_alternative<SandboxConfig>(currentScenarioConfig_)) {
            lv_obj_t* container = panel->getContentArea();
            if (container) {
                const SandboxConfig& config = std::get<SandboxConfig>(currentScenarioConfig_);
                sandboxControls_ = std::make_unique<SandboxControls>(container, wsService_, config);
            }
        }
    }

    // Always update sandbox controls with latest config.
    if (data.scenario_id == "sandbox" && sandboxControls_
        && std::holds_alternative<SandboxConfig>(data.scenario_config)) {
        const SandboxConfig& config = std::get<SandboxConfig>(data.scenario_config);
        sandboxControls_->updateFromConfig(config);
        sandboxControls_->updateWorldDimensions(data.width, data.height);
    }
}

void SimPlayground::render(const WorldData& data, bool debugDraw)
{
    lv_obj_t* worldContainer = uiManager_->getWorldDisplayArea();

    // Render world state (CellRenderer handles initialization/resize internally).
    renderer_->renderWorldData(data, worldContainer, debugDraw, renderMode_);
}

void SimPlayground::setRenderMode(RenderMode mode)
{
    renderMode_ = mode;

    // Sync dropdown to match.
    if (coreControls_) {
        coreControls_->setRenderMode(mode);
    }

    LOG_INFO(Controls, "Render mode set to {}", renderModeToString(mode));
}

void SimPlayground::renderNeuralGrid(const WorldData& data)
{
    // Only render if neural grid is visible.
    if (!uiManager_->isNeuralGridVisible()) {
        return;
    }

    lv_obj_t* neuralGridContainer = uiManager_->getNeuralGridDisplayArea();

    if (data.tree_vision.has_value()) {
        neuralGridRenderer_->renderSensoryData(data.tree_vision.value(), neuralGridContainer);
    }
    else {
        neuralGridRenderer_->renderEmpty(neuralGridContainer);
    }
}

void SimPlayground::onScenarioChanged(lv_event_t* e)
{
    auto* playground = static_cast<SimPlayground*>(
        lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
    if (!playground) return;

    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uint16_t selectedIdx = lv_dropdown_get_selected(dropdown);

    // Map dropdown index to scenario_id (must match dropdown options order).
    const char* scenarioIds[] = { "sandbox",           "dam_break", "empty",
                                  "falling_dirt",      "raining",   "tree_germination",
                                  "water_equalization" };

    constexpr size_t SCENARIO_COUNT = 7;
    if (selectedIdx >= SCENARIO_COUNT) {
        LOG_ERROR(Controls, "Invalid scenario index {}", selectedIdx);
        return;
    }

    std::string scenario_id = scenarioIds[selectedIdx];
    LOG_INFO(Controls, "Scenario changed to '{}'", scenario_id);

    // Send sim_run command with new scenario_id to DSSM server.
    if (playground->wsService_ && playground->wsService_->isConnected()) {
        static std::atomic<uint64_t> nextId{ 1 };
        const DirtSim::Api::SimRun::Command cmd{ .timestep = 0.016,
                                                 .max_steps = -1,
                                                 .scenario_id = scenario_id,
                                                 .max_frame_ms = playground->currentMaxFrameMs_ };

        spdlog::info(
            "SimPlayground: Sending sim_run with scenario '{}', max_frame_ms={}",
            scenario_id,
            cmd.max_frame_ms);

        // Send binary command.
        auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
        auto result = playground->wsService_->sendBinary(Network::serialize_envelope(envelope));
        if (result.isError()) {
            LOG_ERROR(Controls, "Failed to send SimRun: {}", result.errorValue());
        }
    }
    else {
        LOG_WARN(Controls, "WebSocket not connected, cannot switch scenario");
    }
}

std::optional<SimPlayground::ScreenshotData> SimPlayground::captureScreenshotPixels()
{
    if (!renderer_) {
        LOG_ERROR(Controls, "Cannot capture screenshot, renderer not initialized");
        return std::nullopt;
    }

    const uint8_t* buffer = renderer_->getCanvasBuffer();
    uint32_t width = renderer_->getCanvasWidth();
    uint32_t height = renderer_->getCanvasHeight();

    if (!buffer || width == 0 || height == 0) {
        LOG_ERROR(Controls, "Cannot capture screenshot, canvas not initialized");
        return std::nullopt;
    }

    // Calculate buffer size (ARGB8888 = 4 bytes per pixel).
    size_t bufferSize = static_cast<size_t>(width) * height * 4;

    // Make a copy of the pixel data.
    ScreenshotData data;
    data.width = width;
    data.height = height;
    data.pixels.resize(bufferSize);
    std::memcpy(data.pixels.data(), buffer, bufferSize);

    LOG_INFO(Controls, "Captured screenshot {}x{} ({} bytes)", width, height, bufferSize);
    return data;
}

} // namespace Ui
} // namespace DirtSim
