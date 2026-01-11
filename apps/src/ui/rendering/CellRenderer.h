#pragma once

#include "RenderMode.h"
#include "core/Cell.h"
#include "core/Vector2i.h"
#include "core/WorldData.h"
#include "lvgl/lvgl.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace DirtSim {
namespace Ui {

class CellRenderer {
public:
    // Callback invoked whenever the canvas is (re)created.
    using CanvasCreatedCallback = std::function<void(lv_obj_t* canvas)>;

    CellRenderer() = default;
    ~CellRenderer();

    /**
     * @brief Set callback to be invoked when canvas is created or recreated.
     *
     * The canvas may be recreated when render mode or container size changes.
     * Use this to re-attach event handlers after recreation.
     */
    void setCanvasCreatedCallback(CanvasCreatedCallback callback);

    void initialize(lv_obj_t* parent, int16_t worldWidth, int16_t worldHeight);
    void resize(lv_obj_t* parent, int16_t worldWidth, int16_t worldHeight);
    void renderWorldData(
        const WorldData& worldData,
        lv_obj_t* parent,
        bool debugDraw,
        RenderMode mode = RenderMode::SHARP);
    void cleanup();

    /**
     * @brief Get canvas buffer data for screenshot capture.
     * @return Pointer to canvas buffer, or nullptr if not initialized.
     */
    const uint8_t* getCanvasBuffer() const { return canvasBuffer_.data(); }

    uint32_t getCanvasWidth() const { return canvasWidth_; }
    uint32_t getCanvasHeight() const { return canvasHeight_; }

    std::optional<Vector2i> pixelToCell(int pixelX, int pixelY) const;
    lv_obj_t* getCanvas() const { return worldCanvas_; }

private:
    // Callback for canvas creation notifications.
    CanvasCreatedCallback canvasCreatedCallback_;

    // Single canvas for entire world grid.
    lv_obj_t* worldCanvas_ = nullptr;
    std::vector<uint8_t> canvasBuffer_;

    // Canvas dimensions (fixed size)
    uint32_t canvasWidth_ = 0;
    uint32_t canvasHeight_ = 0;

    // World dimensions (variable)
    int16_t width_ = 0;
    int16_t height_ = 0;
    lv_obj_t* parent_ = nullptr;

    // Track container size for resize detection.
    int32_t lastContainerWidth_ = 0;
    int32_t lastContainerHeight_ = 0;

    // Scaled cell dimensions for fitting the drawing area.
    uint32_t scaledCellWidth_ = Cell::WIDTH;
    uint32_t scaledCellHeight_ = Cell::HEIGHT;
    double scaleX_ = 1.0;
    double scaleY_ = 1.0;

    // Track current render mode to detect changes requiring reinitialization.
    RenderMode currentMode_ = RenderMode::SHARP;

    // Display scale factor (visual size / buffer size) for coordinate transformation.
    double displayScale_ = 1.0;

    void calculateScaling(int16_t worldWidth, int16_t worldHeight);
    void initializeWithPixelSize(
        lv_obj_t* parent, int16_t worldWidth, int16_t worldHeight, uint32_t pixelsPerCell);

    // LVGL-based cell rendering (used for LVGL_DEBUG mode).
    void renderCellLVGL(
        const Cell& cell,
        const CellDebug& debug,
        lv_layer_t& layer,
        int32_t cellX,
        int32_t cellY,
        bool debugDraw,
        uint32_t color);
};

// Bresenham's line algorithm for fast pixel-based line drawing.
// Exposed for unit testing. Uses only integer math for maximum performance.
void drawLineBresenham(
    uint32_t* pixels,
    uint32_t canvasWidth,
    uint32_t canvasHeight,
    int x0,
    int y0,
    int x1,
    int y1,
    uint32_t color);

// Get/set the scale factor for SHARP rendering mode.
// Scale > 1.0 creates larger canvas (downscaling = sharper).
// Scale < 1.0 creates smaller canvas (upscaling = smoother).
double getSharpScaleFactor();
void setSharpScaleFactor(double scaleFactor);

} // namespace Ui
} // namespace DirtSim
