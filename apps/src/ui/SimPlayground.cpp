#include "SimPlayground.h"
#include "ScenarioMetadataCache.h"
#include "controls/ClockControls.h"
#include "controls/CoreControls.h"
#include "controls/ExpandablePanel.h"
#include "controls/PhysicsPanel.h"
#include "controls/ScenarioPanel.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/ScenarioConfig.h"
#include "core/network/BinaryProtocol.h"
#include "core/network/WebSocketService.h"
#include "rendering/CellRenderer.h"
#include "rendering/NeuralGridRenderer.h"
#include "server/api/CellSet.h"
#include "state-machine/EventSink.h"
#include "ui/UiComponentManager.h"
#include "ui/ui_builders/LVGLBuilder.h"
#include <cstring>
#include <spdlog/spdlog.h>

namespace DirtSim {
namespace Ui {

SimPlayground::SimPlayground(
    UiComponentManager* uiManager,
    Network::WebSocketServiceInterface* wsService,
    EventSink& eventSink,
    FractalAnimator* fractalAnimator)
    : uiManager_(uiManager),
      wsService_(wsService),
      eventSink_(eventSink),
      fractalAnimator_(fractalAnimator)
{
    renderer_ = std::make_unique<CellRenderer>();
    neuralGridRenderer_ = std::make_unique<NeuralGridRenderer>();

    // Register callback to set up event handlers whenever canvas is (re)created.
    renderer_->setCanvasCreatedCallback(
        [this](lv_obj_t* canvas) { setupCanvasEventHandlers(canvas); });

    lv_obj_t* worldContainer = uiManager_->getWorldDisplayArea();
    renderer_->initialize(worldContainer, 10, 10);

    LOG_INFO(Controls, "Initialized");
}

SimPlayground::~SimPlayground()
{
    LOG_INFO(Controls, "Destroyed");
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
    if (selectedId != IconId::NONE && selectedId != IconId::TREE && selectedId != IconId::DUCK) {
        showPanelContent(selectedId);
    }
    else if (selectedId == IconId::NONE) {
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
        case IconId::EVOLUTION:
        case IconId::MUSIC:
        case IconId::NETWORK:
        case IconId::DUCK:
        case IconId::PLAY:
        case IconId::TREE:
        case IconId::GENOME_BROWSER:
        case IconId::TRAINING_RESULTS:
        case IconId::NONE:
            DIRTSIM_ASSERT(false, "Unexpected icon selection in SimRunning state");
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

    activePanel_ = IconId::NONE;
}

void SimPlayground::createCorePanel(lv_obj_t* container)
{
    LOG_DEBUG(Controls, "Creating Core panel");

    coreControls_ = std::make_unique<CoreControls>(
        container, wsService_, eventSink_, coreControlsState_, uiManager_, fractalAnimator_);
}

void SimPlayground::createScenarioPanel(lv_obj_t* container)
{
    LOG_DEBUG(Controls, "Creating Scenario panel");

    // Create display dimensions getter for auto-scaling scenarios.
    // Always returns dimensions as if overlays are minimized, so scenarios are
    // sized for the largest possible display area. This prevents gaps when
    // the rail auto-shrinks.
    DisplayDimensionsGetter dimensionsGetter = [this]() -> DisplayDimensions {
        lv_obj_t* worldArea = uiManager_->getWorldDisplayArea();
        if (worldArea) {
            lv_obj_update_layout(worldArea);
            uint32_t width = static_cast<uint32_t>(lv_obj_get_width(worldArea));
            uint32_t height = static_cast<uint32_t>(lv_obj_get_height(worldArea));

            lv_obj_t* screen = lv_obj_get_screen(worldArea);
            if (screen) {
                uint32_t screenWidth = static_cast<uint32_t>(lv_obj_get_width(screen));
                uint32_t screenHeight = static_cast<uint32_t>(lv_obj_get_height(screen));
                if (screenWidth > width) {
                    width = screenWidth;
                }
                if (screenHeight > height) {
                    height = screenHeight;
                }
            }

            return DisplayDimensions{ width, height };
        }
        return DisplayDimensions{ 760, 480 }; // Fallback assumes minimized rail.
    };

    // Create scenario panel with modal navigation.
    scenarioPanel_ = std::make_unique<ScenarioPanel>(
        container,
        wsService_,
        eventSink_,
        currentScenarioId_,
        currentScenarioConfig_,
        dimensionsGetter);
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
    Scenario::EnumType scenario_id,
    const ScenarioConfig& scenario_config,
    double uiFPS)
{
    // Capture world size from server.
    coreControlsState_.worldSize = data.width;

    // Sync core controls if panel is active.
    if (coreControls_) {
        coreControls_->updateStats(data.fps_server, uiFPS);
        coreControls_->updateFromState();
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
    // Capture debug draw state from server.
    coreControlsState_.debugDrawEnabled = debugDraw;

    lv_obj_t* worldContainer = uiManager_->getWorldDisplayArea();

    // Render world state (CellRenderer handles initialization/resize internally).
    renderer_->renderWorldData(data, worldContainer, debugDraw, coreControlsState_.renderMode);
}

void SimPlayground::setRenderMode(RenderMode mode)
{
    coreControlsState_.renderMode = mode;

    // Sync controls if panel is active.
    if (coreControls_) {
        coreControls_->updateFromState();
    }

    LOG_INFO(Controls, "Render mode set to {}", renderModeToString(mode));
}

InteractionMode SimPlayground::getInteractionMode() const
{
    return coreControlsState_.interactionMode;
}

std::optional<Vector2i> SimPlayground::pixelToCell(int pixelX, int pixelY) const
{
    DIRTSIM_ASSERT(renderer_, "renderer_ must be set");
    return renderer_->pixelToCell(pixelX, pixelY);
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

    // Force layout update to get accurate dimensions after panel/rail changes.
    // Must update from parent level to recalculate flex layout after IconRail resize.
    lv_obj_t* worldArea = uiManager_->getWorldDisplayArea();
    if (!worldArea) {
        return;
    }
    lv_obj_t* parent = lv_obj_get_parent(worldArea);
    if (parent) {
        lv_obj_t* grandparent = lv_obj_get_parent(parent);
        if (grandparent) {
            lv_obj_update_layout(grandparent);
        }
        else {
            lv_obj_update_layout(parent);
        }
    }
    else {
        lv_obj_update_layout(worldArea);
    }

    // Get new display dimensions, adjusted for minimized overlay state.
    uint32_t newWidth = static_cast<uint32_t>(lv_obj_get_width(worldArea));
    uint32_t newHeight = static_cast<uint32_t>(lv_obj_get_height(worldArea));

    lv_obj_t* screen = lv_obj_get_screen(worldArea);
    if (screen) {
        uint32_t screenWidth = static_cast<uint32_t>(lv_obj_get_width(screen));
        uint32_t screenHeight = static_cast<uint32_t>(lv_obj_get_height(screen));
        if (screenWidth > newWidth) {
            newWidth = screenWidth;
        }
        if (screenHeight > newHeight) {
            newHeight = screenHeight;
        }
    }

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

void SimPlayground::setupCanvasEventHandlers(lv_obj_t* canvas)
{
    DIRTSIM_ASSERT(canvas, "Canvas must be initialized before setting up event handlers");

    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_event_cb(canvas, onCanvasClicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(canvas, onCanvasClicked, LV_EVENT_PRESSING, this);

    LOG_INFO(Controls, "Canvas event handlers installed");
}

void SimPlayground::onCanvasClicked(lv_event_t* e)
{
    SimPlayground* self = static_cast<SimPlayground*>(lv_event_get_user_data(e));
    if (!self) return;

    InteractionMode mode = self->coreControlsState_.interactionMode;

    // Only process events in DRAW or ERASE mode.
    if (mode != InteractionMode::DRAW && mode != InteractionMode::ERASE) {
        return;
    }

    lv_indev_t* indev = lv_event_get_indev(e);
    if (!indev) return;

    lv_point_t screenPoint;
    lv_indev_get_point(indev, &screenPoint);

    lv_obj_t* canvas = self->renderer_->getCanvas();
    DIRTSIM_ASSERT(canvas, "Canvas must exist when event fires");

    lv_area_t canvasArea;
    lv_obj_get_coords(canvas, &canvasArea);

    lv_point_t canvasPoint;
    canvasPoint.x = screenPoint.x - canvasArea.x1;
    canvasPoint.y = screenPoint.y - canvasArea.y1;

    auto cell = self->renderer_->pixelToCell(canvasPoint.x, canvasPoint.y);
    if (!cell) {
        self->lastPaintedCell_ = Vector2i{ -1, -1 };
        return;
    }

    if (cell->x == self->lastPaintedCell_.x && cell->y == self->lastPaintedCell_.y) {
        return;
    }
    self->lastPaintedCell_ = *cell;

    // DRAW mode places the selected material, ERASE mode places AIR.
    Material::EnumType material = (mode == InteractionMode::ERASE)
        ? Material::EnumType::Air
        : self->coreControlsState_.drawMaterial;
    double fillRatio = (mode == InteractionMode::ERASE) ? 0.0 : 1.0;

    static std::atomic<uint64_t> nextId{ 1 };
    Api::CellSet::Command cmd{ cell->x, cell->y, material, fillRatio };
    auto envelope = Network::make_command_envelope(nextId.fetch_add(1), cmd);
    self->wsService_->sendBinary(Network::serialize_envelope(envelope));

    LOG_INFO(
        Controls,
        "{} ({}, {}) -> {}",
        (mode == InteractionMode::ERASE) ? "Erase" : "Draw",
        cell->x,
        cell->y,
        toString(material));
}

} // namespace Ui
} // namespace DirtSim
