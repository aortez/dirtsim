#include "SimPlayground.h"
#include "ScenarioMetadataCache.h"
#include "controls/ClockControls.h"
#include "controls/CoreControls.h"
#include "controls/ExpandablePanel.h"
#include "controls/PhysicsPanel.h"
#include "controls/ScenarioPanel.h"
#include "core/LoggingChannels.h"
#include "core/ScenarioConfig.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "rendering/CellRenderer.h"
#include "rendering/NeuralGridRenderer.h"
#include "state-machine/EventSink.h"
#include "ui/UiComponentManager.h"
#include "ui/ui_builders/LVGLBuilder.h"
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
    scenarioPanel_.reset();

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

    // Create scenario panel with modal navigation.
    scenarioPanel_ = std::make_unique<ScenarioPanel>(
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

    // Store current scenario info.
    currentScenarioId_ = scenario_id;
    currentScenarioConfig_ = scenario_config;

    // Update scenario panel if active.
    if (scenarioPanel_) {
        scenarioPanel_->updateFromConfig(scenario_id, scenario_config);
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
    if (!std::holds_alternative<Config::Clock>(currentScenarioConfig_)) {
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
    Config::Clock config = std::get<Config::Clock>(currentScenarioConfig_);
    if (config.targetDisplayWidth == newWidth && config.targetDisplayHeight == newHeight) {
        // No change in dimensions.
        return;
    }

    LOG_INFO(
        Controls,
        "Display resized: {}x{} -> {}x{}, sending config update",
        config.targetDisplayWidth,
        config.targetDisplayHeight,
        newWidth,
        newHeight);

    config.targetDisplayWidth = newWidth;
    config.targetDisplayHeight = newHeight;

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
