#pragma once

#include "ui/rendering/maze/MazeSearchAnimator.h"
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {

class MazeSearchView {
public:
    enum class PresentationStyle {
        Scene,
        IconBadge,
    };

    enum class ViewportMode {
        FullMaze,
        CenteredSquare,
    };

    MazeSearchView(
        lv_obj_t* parent,
        MazeSearchAnimator& animator,
        ViewportMode viewportMode,
        PresentationStyle presentationStyle = PresentationStyle::Scene);
    ~MazeSearchView();

    void render();

    lv_obj_t* canvas() const { return canvas_; }

private:
    struct Viewport {
        int cropHeight = 0;
        int cropWidth = 0;
        int cropX = 0;
        int cropY = 0;
    };

    void drawCells(
        const MazeSearchAnimator::Snapshot& snapshot, const Viewport& viewport, lv_layer_t& layer);
    void drawPath(
        const MazeSearchAnimator::Snapshot& snapshot, const Viewport& viewport, lv_layer_t& layer);
    void drawWalls(
        const MazeSearchAnimator::Snapshot& snapshot, const Viewport& viewport, lv_layer_t& layer);
    Viewport computeViewport(const MazeModel& model) const;
    bool isCellVisible(const MazeCoord& coord, const Viewport& viewport) const;
    void maybeResize();
    lv_area_t rectForCell(
        const MazeCoord& coord, const Viewport& viewport, float cellSize, float insetRatio) const;
    lv_point_t pointForCellCenter(
        const MazeCoord& coord, const Viewport& viewport, float cellSize) const;
    void renderEmpty();
    void renderSnapshot(const MazeSearchAnimator::Snapshot& snapshot);
    void resize(int width, int height);

    MazeSearchAnimator& animator_;
    lv_obj_t* canvas_ = nullptr;
    lv_color_t* canvasBuffer_ = nullptr;
    int height_ = 0;
    lv_obj_t* parent_ = nullptr;
    PresentationStyle presentationStyle_ = PresentationStyle::Scene;
    ViewportMode viewportMode_ = ViewportMode::FullMaze;
    int width_ = 0;
};

} // namespace Ui
} // namespace DirtSim
