#include "MazeSearchView.h"
#include "core/Assert.h"
#include "core/LoggingChannels.h"
#include <algorithm>

namespace DirtSim {
namespace Ui {
namespace {

struct MazePalette {
    uint32_t backgroundColor = 0;
    uint32_t borderColor = 0;
    uint32_t frontierColor = 0;
    uint32_t generationColor = 0;
    uint32_t goalColor = 0;
    uint32_t pathColor = 0;
    uint32_t startColor = 0;
    uint32_t visitedColor = 0;
    uint32_t wallColor = 0;
};

constexpr MazePalette kScenePalette{
    .backgroundColor = 0x04070A,
    .borderColor = 0x22303B,
    .frontierColor = 0x57DBFF,
    .generationColor = 0x101B25,
    .goalColor = 0xFFD166,
    .pathColor = 0xFFE08A,
    .startColor = 0x8FE388,
    .visitedColor = 0x1D3F58,
    .wallColor = 0x425E71,
};

constexpr MazePalette kIconPalette{
    .backgroundColor = 0x0A0F14,
    .borderColor = 0x5B6F7E,
    .frontierColor = 0x77E7FF,
    .generationColor = 0x1A2631,
    .goalColor = 0xFFD166,
    .pathColor = 0xFFE59E,
    .startColor = 0xA5EE96,
    .visitedColor = 0x28546F,
    .wallColor = 0x7E93A3,
};

float computeCellSize(
    int width, int height, int cropWidth, int cropHeight, MazeSearchView::PresentationStyle style)
{
    const int padding = style == MazeSearchView::PresentationStyle::IconBadge ? 10 : 24;
    const float contentWidth = static_cast<float>(std::max(1, width - padding));
    const float contentHeight = static_cast<float>(std::max(1, height - padding));
    return std::max(
        1.0f,
        std::min(
            contentWidth / static_cast<float>(std::max(1, cropWidth)),
            contentHeight / static_cast<float>(std::max(1, cropHeight))));
}

const MazePalette& paletteForStyle(MazeSearchView::PresentationStyle style)
{
    return style == MazeSearchView::PresentationStyle::IconBadge ? kIconPalette : kScenePalette;
}

} // namespace

MazeSearchView::MazeSearchView(
    lv_obj_t* parent,
    MazeSearchAnimator& animator,
    ViewportMode viewportMode,
    PresentationStyle presentationStyle)
    : animator_(animator),
      parent_(parent),
      presentationStyle_(presentationStyle),
      viewportMode_(viewportMode)
{
    DIRTSIM_ASSERT(parent_, "MazeSearchView requires a parent");

    canvas_ = lv_canvas_create(parent_);
    DIRTSIM_ASSERT(canvas_, "MazeSearchView failed to create canvas");

    lv_obj_set_size(canvas_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(canvas_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(canvas_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_opa(canvas_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(canvas_, 0, 0);
    lv_obj_move_to_index(canvas_, 0);

    maybeResize();
    renderEmpty();
}

MazeSearchView::~MazeSearchView()
{
    if (canvas_ && lv_obj_is_valid(canvas_)) {
        lv_obj_del(canvas_);
    }
    canvas_ = nullptr;

    if (canvasBuffer_) {
        lv_free(canvasBuffer_);
        canvasBuffer_ = nullptr;
    }
}

void MazeSearchView::render()
{
    maybeResize();
    renderSnapshot(animator_.snapshot());
}

void MazeSearchView::drawCells(
    const MazeSearchAnimator::Snapshot& snapshot, const Viewport& viewport, lv_layer_t& layer)
{
    DIRTSIM_ASSERT(snapshot.model, "MazeSearchView snapshot model must exist");

    const MazePalette& palette = paletteForStyle(presentationStyle_);
    const float cellSize = computeCellSize(
        width_, height_, viewport.cropWidth, viewport.cropHeight, presentationStyle_);
    const MazeModel& model = *snapshot.model;
    for (int index = 0; index < model.cellCount(); ++index) {
        const MazeCoord coord = model.coordForIndex(index);
        if (!isCellVisible(coord, viewport)) {
            continue;
        }

        const bool generationVisited = snapshot.generationVisited
            && snapshot.generationVisited->at(static_cast<size_t>(index)) != 0;
        const bool solverVisited =
            snapshot.solverVisited && snapshot.solverVisited->at(static_cast<size_t>(index)) != 0;
        const bool frontier =
            snapshot.frontierFlags && snapshot.frontierFlags->at(static_cast<size_t>(index)) != 0;

        uint32_t fillColor = 0;
        float insetRatio = 0.10f;
        if (solverVisited) {
            fillColor = palette.visitedColor;
            insetRatio = 0.08f;
        }
        else if (generationVisited) {
            fillColor = palette.generationColor;
            insetRatio = 0.08f;
        }

        if (fillColor != 0) {
            lv_draw_rect_dsc_t rectDsc;
            lv_draw_rect_dsc_init(&rectDsc);
            rectDsc.bg_color = lv_color_hex(fillColor);
            rectDsc.bg_opa = LV_OPA_COVER;
            rectDsc.border_width = 0;
            const lv_area_t cellRect = rectForCell(coord, viewport, cellSize, insetRatio);
            lv_draw_rect(&layer, &rectDsc, &cellRect);
        }

        if (index == snapshot.activeCellIndex) {
            lv_draw_rect_dsc_t rectDsc;
            lv_draw_rect_dsc_init(&rectDsc);
            rectDsc.bg_color = lv_color_hex(palette.goalColor);
            rectDsc.bg_opa = LV_OPA_COVER;
            rectDsc.border_width = 0;
            const lv_area_t cellRect = rectForCell(coord, viewport, cellSize, 0.24f);
            lv_draw_rect(&layer, &rectDsc, &cellRect);
        }

        if (frontier) {
            lv_draw_rect_dsc_t rectDsc;
            lv_draw_rect_dsc_init(&rectDsc);
            rectDsc.bg_color = lv_color_hex(palette.frontierColor);
            rectDsc.bg_opa = LV_OPA_COVER;
            rectDsc.border_width = 0;
            const lv_area_t cellRect = rectForCell(coord, viewport, cellSize, 0.18f);
            lv_draw_rect(&layer, &rectDsc, &cellRect);
        }

        if (index == model.startIndex() || index == model.goalIndex()) {
            lv_draw_rect_dsc_t rectDsc;
            lv_draw_rect_dsc_init(&rectDsc);
            rectDsc.bg_color =
                lv_color_hex(index == model.startIndex() ? palette.startColor : palette.goalColor);
            rectDsc.bg_opa = LV_OPA_COVER;
            rectDsc.border_width = 0;
            const lv_area_t cellRect = rectForCell(coord, viewport, cellSize, 0.22f);
            lv_draw_rect(&layer, &rectDsc, &cellRect);
        }
    }
}

void MazeSearchView::drawPath(
    const MazeSearchAnimator::Snapshot& snapshot, const Viewport& viewport, lv_layer_t& layer)
{
    if (!snapshot.model || !snapshot.solutionPath || snapshot.revealedSolutionLength < 2) {
        return;
    }

    const MazePalette& palette = paletteForStyle(presentationStyle_);
    const float cellSize = computeCellSize(
        width_, height_, viewport.cropWidth, viewport.cropHeight, presentationStyle_);
    const MazeModel& model = *snapshot.model;
    const size_t pathLength =
        std::min(snapshot.revealedSolutionLength, snapshot.solutionPath->size());
    for (size_t i = 1; i < pathLength; ++i) {
        const MazeCoord fromCoord = model.coordForIndex(snapshot.solutionPath->at(i - 1));
        const MazeCoord toCoord = model.coordForIndex(snapshot.solutionPath->at(i));
        if (!isCellVisible(fromCoord, viewport) && !isCellVisible(toCoord, viewport)) {
            continue;
        }

        lv_draw_line_dsc_t lineDsc;
        lv_draw_line_dsc_init(&lineDsc);
        lineDsc.color = lv_color_hex(palette.pathColor);
        lineDsc.opa = LV_OPA_COVER;
        lineDsc.width = std::max(2, static_cast<int>(cellSize * 0.34f));
        const lv_point_t fromPoint = pointForCellCenter(fromCoord, viewport, cellSize);
        const lv_point_t toPoint = pointForCellCenter(toCoord, viewport, cellSize);
        lineDsc.p1.x = fromPoint.x;
        lineDsc.p1.y = fromPoint.y;
        lineDsc.p2.x = toPoint.x;
        lineDsc.p2.y = toPoint.y;
        lv_draw_line(&layer, &lineDsc);
    }
}

void MazeSearchView::drawWalls(
    const MazeSearchAnimator::Snapshot& snapshot, const Viewport& viewport, lv_layer_t& layer)
{
    DIRTSIM_ASSERT(snapshot.model, "MazeSearchView snapshot model must exist");

    const MazePalette& palette = paletteForStyle(presentationStyle_);
    const float cellSize = computeCellSize(
        width_, height_, viewport.cropWidth, viewport.cropHeight, presentationStyle_);
    const MazeModel& model = *snapshot.model;
    for (int index = 0; index < model.cellCount(); ++index) {
        const MazeCoord coord = model.coordForIndex(index);
        if (!isCellVisible(coord, viewport)) {
            continue;
        }

        const MazeCell& cell = model.cellAt(index);
        const bool generationVisited = snapshot.generationVisited
            && snapshot.generationVisited->at(static_cast<size_t>(index)) != 0;
        if (snapshot.phase == MazeSearchAnimator::Phase::BuildingMaze && !generationVisited) {
            continue;
        }

        const lv_point_t center = pointForCellCenter(coord, viewport, cellSize);
        const int halfCell = static_cast<int>(cellSize * 0.5f);
        const int left = center.x - halfCell;
        const int right = center.x + halfCell;
        const int top = center.y - halfCell;
        const int bottom = center.y + halfCell;

        auto drawSegment = [&](int x1, int y1, int x2, int y2) {
            lv_draw_line_dsc_t lineDsc;
            lv_draw_line_dsc_init(&lineDsc);
            lineDsc.color = lv_color_hex(palette.wallColor);
            lineDsc.opa = LV_OPA_COVER;
            lineDsc.width = std::max(1, static_cast<int>(cellSize * 0.16f));
            lineDsc.p1.x = x1;
            lineDsc.p1.y = y1;
            lineDsc.p2.x = x2;
            lineDsc.p2.y = y2;
            lv_draw_line(&layer, &lineDsc);
        };

        if ((cell.openings & mazeDirectionBit(MazeDirection::North)) == 0) {
            drawSegment(left, top, right, top);
        }
        if ((cell.openings & mazeDirectionBit(MazeDirection::West)) == 0) {
            drawSegment(left, top, left, bottom);
        }
        if ((cell.openings & mazeDirectionBit(MazeDirection::South)) == 0
            || coord.y == viewport.cropY + viewport.cropHeight - 1) {
            drawSegment(left, bottom, right, bottom);
        }
        if ((cell.openings & mazeDirectionBit(MazeDirection::East)) == 0
            || coord.x == viewport.cropX + viewport.cropWidth - 1) {
            drawSegment(right, top, right, bottom);
        }
    }
}

MazeSearchView::Viewport MazeSearchView::computeViewport(
    const MazeSearchAnimator::Snapshot& snapshot) const
{
    DIRTSIM_ASSERT(snapshot.model, "MazeSearchView snapshot model must exist");
    const MazeModel& model = *snapshot.model;

    if (viewportMode_ == ViewportMode::FullMaze) {
        return Viewport{
            .cropHeight = model.height(),
            .cropWidth = model.width(),
            .cropX = 0,
            .cropY = 0,
        };
    }

    const int cropSize = std::min(model.width(), model.height());
    return Viewport{
        .cropHeight = cropSize,
        .cropWidth = cropSize,
        .cropX = (model.width() - cropSize) / 2,
        .cropY = (model.height() - cropSize) / 2,
    };
}

bool MazeSearchView::isCellVisible(const MazeCoord& coord, const Viewport& viewport) const
{
    return coord.x >= viewport.cropX && coord.y >= viewport.cropY
        && coord.x < viewport.cropX + viewport.cropWidth
        && coord.y < viewport.cropY + viewport.cropHeight;
}

void MazeSearchView::maybeResize()
{
    if (!parent_) {
        return;
    }

    const int newWidth = lv_obj_get_width(parent_);
    const int newHeight = lv_obj_get_height(parent_);
    if (newWidth <= 0 || newHeight <= 0) {
        return;
    }

    if (newWidth != width_ || newHeight != height_) {
        resize(newWidth, newHeight);
    }
}

lv_area_t MazeSearchView::rectForCell(
    const MazeCoord& coord, const Viewport& viewport, float cellSize, float insetRatio) const
{
    const float renderWidth = cellSize * static_cast<float>(viewport.cropWidth);
    const float renderHeight = cellSize * static_cast<float>(viewport.cropHeight);
    const float originX = (static_cast<float>(width_) - renderWidth) * 0.5f;
    const float originY = (static_cast<float>(height_) - renderHeight) * 0.5f;
    const float relativeX = static_cast<float>(coord.x - viewport.cropX);
    const float relativeY = static_cast<float>(coord.y - viewport.cropY);
    const float inset = cellSize * insetRatio;

    const int x1 = static_cast<int>(originX + relativeX * cellSize + inset);
    const int y1 = static_cast<int>(originY + relativeY * cellSize + inset);
    const int x2 = static_cast<int>(originX + (relativeX + 1.0f) * cellSize - inset) - 1;
    const int y2 = static_cast<int>(originY + (relativeY + 1.0f) * cellSize - inset) - 1;
    return lv_area_t{
        .x1 = std::min(x1, x2),
        .y1 = std::min(y1, y2),
        .x2 = std::max(x1, x2),
        .y2 = std::max(y1, y2),
    };
}

lv_point_t MazeSearchView::pointForCellCenter(
    const MazeCoord& coord, const Viewport& viewport, float cellSize) const
{
    const float renderWidth = cellSize * static_cast<float>(viewport.cropWidth);
    const float renderHeight = cellSize * static_cast<float>(viewport.cropHeight);
    const float originX = (static_cast<float>(width_) - renderWidth) * 0.5f;
    const float originY = (static_cast<float>(height_) - renderHeight) * 0.5f;
    const float relativeX = static_cast<float>(coord.x - viewport.cropX);
    const float relativeY = static_cast<float>(coord.y - viewport.cropY);
    return lv_point_t{
        .x = static_cast<lv_coord_t>(originX + (relativeX + 0.5f) * cellSize),
        .y = static_cast<lv_coord_t>(originY + (relativeY + 0.5f) * cellSize),
    };
}

void MazeSearchView::renderEmpty()
{
    if (!canvas_) {
        return;
    }

    const MazePalette& palette = paletteForStyle(presentationStyle_);
    lv_canvas_fill_bg(canvas_, lv_color_hex(palette.backgroundColor), LV_OPA_COVER);
}

void MazeSearchView::renderSnapshot(const MazeSearchAnimator::Snapshot& snapshot)
{
    if (!canvas_) {
        return;
    }

    const MazePalette& palette = paletteForStyle(presentationStyle_);
    lv_canvas_fill_bg(canvas_, lv_color_hex(palette.backgroundColor), LV_OPA_COVER);
    if (!snapshot.hasMaze()) {
        return;
    }

    const Viewport viewport = computeViewport(snapshot);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);
    if (presentationStyle_ == PresentationStyle::IconBadge) {
        lv_draw_rect_dsc_t frameDsc;
        lv_draw_rect_dsc_init(&frameDsc);
        frameDsc.bg_opa = LV_OPA_TRANSP;
        frameDsc.border_color = lv_color_hex(palette.borderColor);
        frameDsc.border_opa = LV_OPA_COVER;
        frameDsc.border_width = 2;
        frameDsc.radius = 10;
        const lv_area_t frameRect{
            .x1 = 1,
            .y1 = 1,
            .x2 = width_ - 2,
            .y2 = height_ - 2,
        };
        lv_draw_rect(&layer, &frameDsc, &frameRect);
    }
    drawCells(snapshot, viewport, layer);
    drawPath(snapshot, viewport, layer);
    drawWalls(snapshot, viewport, layer);
    lv_canvas_finish_layer(canvas_, &layer);
}

void MazeSearchView::resize(int width, int height)
{
    if (width <= 0 || height <= 0 || !canvas_) {
        return;
    }

    const size_t bufferSize = LV_CANVAS_BUF_SIZE(width, height, 32, 64);
    auto* newBuffer = static_cast<lv_color_t*>(lv_malloc(bufferSize));
    if (!newBuffer) {
        LOG_ERROR(Render, "MazeSearchView failed to allocate canvas buffer");
        return;
    }

    if (canvasBuffer_) {
        lv_free(canvasBuffer_);
    }

    canvasBuffer_ = newBuffer;
    width_ = width;
    height_ = height;
    lv_obj_set_size(canvas_, width_, height_);
    lv_canvas_set_buffer(canvas_, canvasBuffer_, width_, height_, LV_COLOR_FORMAT_ARGB8888);
    const MazePalette& palette = paletteForStyle(presentationStyle_);
    lv_canvas_fill_bg(canvas_, lv_color_hex(palette.backgroundColor), LV_OPA_COVER);
}

} // namespace Ui
} // namespace DirtSim
