#include "SimPlayground.h"
#include "ScenarioMetadataCache.h"
#include "controls/ClockControls.h"
#include "controls/CoreControls.h"
#include "controls/ExpandablePanel.h"
#include "controls/PhysicsPanel.h"
#include "controls/ScenarioControlsFactory.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "rendering/CellRenderer.h"
#include "rendering/NeuralGridRenderer.h"
#include "server/api/ScenarioConfigSet.h"
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

        // For auto-scaling scenarios like Clock, trigger a resize now that
        // the panel is closed and more display space is available.
        sendDisplayResizeUpdate();
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

    // For auto-scaling scenarios like Clock, trigger a resize now that the
    // panel is open and less display space is available.
    sendDisplayResizeUpdate();
}

void SimPlayground::clearPanelContent()
{
    // Reset panel-specific controls.
    coreControls_.reset();
    physicsPanel_.reset();
    scenarioControls_.reset();
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

    // Scenario title with emphasis.
    lv_obj_t* scenarioLabel = lv_label_create(container);
    lv_label_set_text(scenarioLabel, "Scenario");
    lv_obj_set_style_text_color(scenarioLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(scenarioLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_decor(scenarioLabel, LV_TEXT_DECOR_UNDERLINE, 0);
    lv_obj_set_style_pad_bottom(scenarioLabel, 8, 0);

    // Build dropdown from metadata cache.
    std::string dropdownOptions = ScenarioMetadataCache::buildDropdownOptions();
    uint16_t selectedIdx = ScenarioMetadataCache::indexFromScenarioId(currentScenarioId_);

    scenarioDropdown_ = LVGLBuilder::dropdown(container)
                            .options(dropdownOptions.c_str())
                            .selected(selectedIdx)
                            .size(LV_PCT(95), LVGLBuilder::Style::CONTROL_HEIGHT)
                            .buildOrLog();

    if (scenarioDropdown_) {
        // Style the dropdown button (light green background, dark purple text).
        lv_obj_set_style_bg_color(
            scenarioDropdown_, lv_color_hex(0x90EE90), LV_PART_MAIN); // Light green.
        lv_obj_set_style_text_color(
            scenarioDropdown_, lv_color_hex(0x4B0082), LV_PART_MAIN); // Dark purple (indigo).

        // Style the dropdown list (when opened).
        lv_obj_t* list = lv_dropdown_get_list(scenarioDropdown_);
        lv_obj_set_style_bg_color(list, lv_color_hex(0x90EE90), LV_PART_MAIN); // Light green.
        lv_obj_set_style_text_color(list, lv_color_hex(0x4B0082), LV_PART_MAIN); // Dark purple.

        // Make list items touch-friendly height.
        lv_obj_set_style_text_line_space(list, LVGLBuilder::Style::CONTROL_HEIGHT - 16, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(list, 8, LV_PART_MAIN);

        // Set max height to fit all scenarios (7 items × ~48px each + padding).
        lv_obj_set_style_max_height(list, 400, LV_PART_MAIN);

        lv_obj_set_user_data(scenarioDropdown_, this);
        lv_obj_add_event_cb(scenarioDropdown_, onScenarioChanged, LV_EVENT_VALUE_CHANGED, this);
    }

    // Create display dimensions getter for auto-scaling scenarios.
    DisplayDimensionsGetter dimensionsGetter = [this]() -> DisplayDimensions {
        lv_obj_t* worldArea = uiManager_->getWorldDisplayArea();
        if (worldArea) {
            lv_obj_update_layout(worldArea);
            return DisplayDimensions{
                static_cast<uint32_t>(lv_obj_get_width(worldArea)),
                static_cast<uint32_t>(lv_obj_get_height(worldArea))};
        }
        return DisplayDimensions{692, 480}; // Fallback to reasonable defaults.
    };

    // Create scenario-specific controls using factory.
    scenarioControls_ = ScenarioControlsFactory::create(
        container, wsService_, currentScenarioId_, currentScenarioConfig_, dimensionsGetter);
}

void SimPlayground::createPhysicsPanel(lv_obj_t* container)
{
    LOG_DEBUG(Controls, "Creating Physics panel");

    // Physics title with emphasis.
    lv_obj_t* title = lv_label_create(container);
    lv_label_set_text(title, "Physics Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_decor(title, LV_TEXT_DECOR_UNDERLINE, 0);
    lv_obj_set_style_pad_bottom(title, 8, 0);

    physicsPanel_ = std::make_unique<PhysicsPanel>(container, wsService_);
}

void SimPlayground::updatePhysicsPanels(const PhysicsSettings& settings)
{
    if (physicsPanel_) {
        physicsPanel_->updateFromSettings(settings);
    }
}

void SimPlayground::updateFromWorldData(
    const WorldData& data,
    const std::string& scenario_id,
    const ScenarioConfig& scenario_config,
    double uiFPS)
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
    if (scenario_id != currentScenarioId_) {
        LOG_INFO(Controls, "Scenario changed to '{}'", scenario_id);

        // Clear old scenario controls.
        scenarioControls_.reset();

        currentScenarioId_ = scenario_id;
    }

    // Store the config for use when panel is opened.
    currentScenarioConfig_ = scenario_config;

    // Create scenario controls if scenario panel is active and controls don't exist.
    if (activePanel_ == IconId::SCENARIO && !scenarioControls_) {
        ExpandablePanel* panel = uiManager_->getExpandablePanel();
        if (panel) {
            lv_obj_t* container = panel->getContentArea();
            if (container) {
                // Create display dimensions getter for auto-scaling scenarios.
                DisplayDimensionsGetter dimensionsGetter = [this]() -> DisplayDimensions {
                    lv_obj_t* worldArea = uiManager_->getWorldDisplayArea();
                    if (worldArea) {
                        lv_obj_update_layout(worldArea);
                        return DisplayDimensions{
                            static_cast<uint32_t>(lv_obj_get_width(worldArea)),
                            static_cast<uint32_t>(lv_obj_get_height(worldArea))};
                    }
                    return DisplayDimensions{692, 480}; // Fallback to reasonable defaults.
                };

                scenarioControls_ = ScenarioControlsFactory::create(
                    container, wsService_, currentScenarioId_, currentScenarioConfig_, dimensionsGetter);
            }
        }
    }

    // Always update scenario controls with latest config.
    if (scenarioControls_) {
        scenarioControls_->updateFromConfig(scenario_config);
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

    // Map dropdown index to scenario_id using metadata cache.
    std::string scenario_id = ScenarioMetadataCache::scenarioIdFromIndex(selectedIdx);
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

void SimPlayground::sendDisplayResizeUpdate()
{
    // Only send resize for auto-scaling scenarios like Clock.
    if (!std::holds_alternative<ClockConfig>(currentScenarioConfig_)) {
        return;
    }

    if (!wsService_ || !wsService_->isConnected()) {
        return;
    }

    // Force layout update to get accurate dimensions after panel closed.
    lv_obj_t* worldArea = uiManager_->getWorldDisplayArea();
    if (!worldArea) {
        return;
    }
    lv_obj_update_layout(worldArea);

    // Get new display dimensions.
    uint32_t newWidth = static_cast<uint32_t>(lv_obj_get_width(worldArea));
    uint32_t newHeight = static_cast<uint32_t>(lv_obj_get_height(worldArea));

    // Update the config with new dimensions.
    ClockConfig config = std::get<ClockConfig>(currentScenarioConfig_);
    if (config.target_display_width == newWidth && config.target_display_height == newHeight) {
        // No change in dimensions.
        return;
    }

    LOG_INFO(
        Controls,
        "Display resized: {}x{} -> {}x{}, sending config update",
        config.target_display_width,
        config.target_display_height,
        newWidth,
        newHeight);

    config.target_display_width = newWidth;
    config.target_display_height = newHeight;

    // Send the updated config to the server.
    const Api::ScenarioConfigSet::Command cmd{ .config = config };
    static std::atomic<uint64_t> nextId{ 1 };
    auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
    auto result = wsService_->sendBinary(Network::serialize_envelope(envelope));
    if (result.isError()) {
        LOG_ERROR(Controls, "Failed to send display resize update: {}", result.errorValue());
    }
}

} // namespace Ui
} // namespace DirtSim
