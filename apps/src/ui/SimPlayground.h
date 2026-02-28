#pragma once

#include "core/ScenarioConfig.h"
#include "core/Vector2i.h"
#include "core/WorldData.h"
#include "ui/InteractionMode.h"
#include "ui/controls/CoreControls.h"
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
class WebSocketServiceInterface;
}

namespace Ui {

class UiComponentManager;
class CoreControls;
class ScenarioPanel;
class PhysicsPanel;
class CellRenderer;
class NeuralGridRenderer;
class EventSink;
class ExpandablePanel;
class FractalAnimator;
class UserSettingsManager;

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
        UiComponentManager* uiManager,
        Network::WebSocketServiceInterface* wsService,
        UserSettingsManager& userSettingsManager,
        EventSink& eventSink,
        FractalAnimator* fractalAnimator);
    ~SimPlayground();

    /**
     * @brief Handle icon selection change from state machine.
     * Shows/hides panel content based on selected icon.
     */
    void onIconSelected(IconId selectedId, IconId previousId);

    /**
     * @brief Send display resize update for auto-scaling scenarios.
     * Called when the panel closes or rail mode changes.
     */
    void sendDisplayResizeUpdate();

    void updateFromWorldData(
        const WorldData& data,
        Scenario::EnumType scenario_id,
        const ScenarioConfig& scenario_config,
        double uiFPS = 0.0);

    void render(const WorldData& data, bool debugDraw);

    void setRenderMode(RenderMode mode);

    RenderMode getRenderMode() const { return coreControlsState_.renderMode; }

    InteractionMode getInteractionMode() const;
    std::optional<Vector2i> pixelToCell(int pixelX, int pixelY) const;

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
    Network::WebSocketServiceInterface* wsService_;
    UserSettingsManager& userSettingsManager_;
    EventSink& eventSink_;
    FractalAnimator* fractalAnimator_ = nullptr;

    // State for CoreControls that persists across panel switches.
    CoreControlsState coreControlsState_;

    // Renderers (always active).
    std::unique_ptr<CellRenderer> renderer_;
    std::unique_ptr<NeuralGridRenderer> neuralGridRenderer_;

    // Panel content (created lazily, one at a time).
    std::unique_ptr<CoreControls> coreControls_;
    std::unique_ptr<ScenarioPanel> scenarioPanel_;
    std::unique_ptr<PhysicsPanel> physicsPanel_;

    // Currently active panel.
    IconId activePanel_ = IconId::NONE;

    // Current scenario ID (to detect changes).
    Scenario::EnumType currentScenarioId_ = Scenario::EnumType::Empty;

    // Current scenario config (to detect changes).
    ScenarioConfig currentScenarioConfig_;

    // Current frame limit.
    int currentMaxFrameMs_ = 16;

    // Track if tree exists (for icon visibility).
    bool treeExists_ = false;

    // Track last painted cell for deduplication.
    Vector2i lastPaintedCell_{ -1, -1 };

    void clearPanelContent();

    void createCorePanel(lv_obj_t* container);

    void createScenarioPanel(lv_obj_t* container);

    void createPhysicsPanel(lv_obj_t* container);

    void showPanelContent(IconId panelId);

    void setupCanvasEventHandlers(lv_obj_t* canvas);
    static void onCanvasClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
