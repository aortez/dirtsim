#pragma once

#include "core/WorldData.h"
#include "ui/rendering/RenderMode.h"

#include <memory>
#include <optional>
#include <vector>

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;

namespace DirtSim {

namespace Network {
class WebSocketService;
}

namespace Ui {

class UiComponentManager;
class CoreControls;
class SandboxControls;
class PhysicsControls;
class CellRenderer;
class NeuralGridRenderer;
class EventSink;

/**
 * @brief Coordinates the simulation playground view.
 *
 * SimPlayground ties together all the UI components for the simulation:
 * - Core controls (quit, stats, debug)
 * - Scenario controls (sandbox toggles)
 * - Physics controls (parameter sliders)
 * - World renderer (cell grid)
 */
class SimPlayground {
public:
    SimPlayground(
        UiComponentManager* uiManager, Network::WebSocketService* wsService, EventSink& eventSink);
    ~SimPlayground();

    void updateFromWorldData(const WorldData& data, double uiFPS = 0.0);

    void render(const WorldData& data, bool debugDraw);

    void setRenderMode(RenderMode mode);

    RenderMode getRenderMode() const { return renderMode_; }

    void renderNeuralGrid(const WorldData& data);

    PhysicsControls* getPhysicsControls() { return physicsControls_.get(); }

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

    // UI components.
    std::unique_ptr<CoreControls> coreControls_;
    std::unique_ptr<SandboxControls> sandboxControls_;
    std::unique_ptr<PhysicsControls> physicsControls_;
    std::unique_ptr<CellRenderer> renderer_;
    std::unique_ptr<NeuralGridRenderer> neuralGridRenderer_;

    lv_obj_t* scenarioDropdown_ = nullptr;

    // Current scenario ID (to detect changes).
    std::string currentScenarioId_;

    // Current scenario config (to detect changes).
    ScenarioConfig currentScenarioConfig_;

    // Current frame limit.
    int currentMaxFrameMs_ = 16;

    // Event handlers.
    static void onScenarioChanged(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
