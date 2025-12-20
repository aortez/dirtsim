#pragma once

#include "core/ScenarioConfig.h"
#include "core/WorldData.h"
#include "ui/controls/IconRail.h"
#include "ui/rendering/RenderMode.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;

namespace DirtSim {

struct PhysicsSettings;

namespace Network {
class WebSocketService;
}

namespace Ui {

class UiComponentManager;
class CoreControls;
class ScenarioControlsBase;
class PhysicsPanel;
class CellRenderer;
class NeuralGridRenderer;
class EventSink;
class ExpandablePanel;

/**
 * @brief Coordinates the simulation playground view.
 *
 * SimPlayground ties together all the UI components for the simulation:
 * - Icon rail for navigation
 * - Expandable panel for controls
 * - World renderer (cell grid)
 * - Neural grid renderer (tree vision)
 *
 * Panel content is created lazily when icons are selected:
 * - Core: Quit, stats, debug, render mode
 * - Scenario: Scenario dropdown + scenario-specific controls
 * - Physics: All physics controls in collapsible sections (General, Pressure, Forces, etc.)
 * - Tree: Toggles neural grid visibility (no panel)
 */
class SimPlayground {
public:
    SimPlayground(
        UiComponentManager* uiManager, Network::WebSocketService* wsService, EventSink& eventSink);
    ~SimPlayground();

    /**
     * @brief Connect to the icon rail's selection callback.
     * Must be called after construction to enable panel switching.
     */
    void connectToIconRail();

    void updateFromWorldData(
        const WorldData& data,
        const std::string& scenario_id,
        const ScenarioConfig& scenario_config,
        double uiFPS = 0.0);

    void render(const WorldData& data, bool debugDraw);

    void setRenderMode(RenderMode mode);

    RenderMode getRenderMode() const { return renderMode_; }

    void renderNeuralGrid(const WorldData& data);

    void updatePhysicsPanels(const PhysicsSettings& settings);

    struct ScreenshotData {
        std::vector<uint8_t> pixels; // ARGB8888 pixel data.
        uint32_t width;
        uint32_t height;
    };

    /**
     * @brief Capture screenshot as raw pixel data.
     * @return Pixel data in ARGB8888 format, or std::nullopt if capture failed.
     */
    std::optional<ScreenshotData> captureScreenshotPixels();

private:
    UiComponentManager* uiManager_;
    RenderMode renderMode_ = RenderMode::ADAPTIVE;
    Network::WebSocketService* wsService_;
    EventSink& eventSink_;

    // Renderers (always active).
    std::unique_ptr<CellRenderer> renderer_;
    std::unique_ptr<NeuralGridRenderer> neuralGridRenderer_;

    // Panel content (created lazily, one at a time).
    std::unique_ptr<CoreControls> coreControls_;
    std::unique_ptr<ScenarioControlsBase> scenarioControls_;
    std::unique_ptr<PhysicsPanel> physicsPanel_;
    lv_obj_t* scenarioDropdown_ = nullptr;

    // Currently active panel.
    IconId activePanel_ = IconId::COUNT;

    // Current scenario ID (to detect changes).
    std::string currentScenarioId_;

    // Current scenario config (to detect changes).
    ScenarioConfig currentScenarioConfig_;

    // Current frame limit.
    int currentMaxFrameMs_ = 16;

    // Track if tree exists (for icon visibility).
    bool treeExists_ = false;

    void clearPanelContent();

    /**
     * @brief Send display resize update for auto-scaling scenarios.
     *
     * Called when the panel closes to notify scenarios like Clock that
     * more display space is now available.
     */
    void sendDisplayResizeUpdate();

    void createCorePanel(lv_obj_t* container);

    void createScenarioPanel(lv_obj_t* container);

    void createPhysicsPanel(lv_obj_t* container);

    void showPanelContent(IconId panelId);

    void onIconSelected(IconId selectedId, IconId previousId);

    static void onScenarioChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
