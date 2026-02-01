#include "FractalAnimator.h"
#include "ui/rendering/JuliaFractal.h"
#include <algorithm>
#include <cstddef>

namespace DirtSim {
namespace Ui {

FractalAnimator::~FractalAnimator()
{
    park();
}

void FractalAnimator::attachTo(lv_obj_t* parent, int width, int height)
{
    attachView(parent, width, height);
}

FractalAnimator::FractalViewId FractalAnimator::attachView(lv_obj_t* parent, int width, int height)
{
    if (!parent || width <= 0 || height <= 0) {
        return InvalidViewId;
    }

    FractalView* view = findViewByParent(parent);
    if (!view) {
        views_.push_back(FractalView{ nextViewId_++, parent, nullptr, width, height });
        view = &views_.back();
    }
    else {
        view->viewWidth = width;
        view->viewHeight = height;
    }

    if (!view->canvas || !lv_obj_is_valid(view->canvas)) {
        view->canvas = lv_canvas_create(parent);
        lv_obj_set_pos(view->canvas, 0, 0);
        lv_obj_move_to_index(view->canvas, 0);
        lv_obj_clear_flag(view->canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view->canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(view->canvas, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(view->canvas, onCanvasDeleted, LV_EVENT_DELETE, this);
    }
    else if (lv_obj_get_parent(view->canvas) != parent) {
        lv_obj_set_parent(view->canvas, parent);
    }

    if (view->canvas && lv_obj_is_valid(view->canvas)) {
        lv_obj_set_pos(view->canvas, 0, 0);
        lv_obj_move_to_index(view->canvas, 0);
        lv_obj_clear_flag(view->canvas, LV_OBJ_FLAG_HIDDEN);
    }

    const bool resized = updateRenderTargetSize();
    if (resized) {
        syncViewsToRenderer(true);
    }
    syncView(*view, true);
    return view->id;
}

bool FractalAnimator::detachView(FractalViewId viewId)
{
    if (viewId == InvalidViewId) {
        return false;
    }

    const size_t viewCount = views_.size();
    removeViewById(viewId, true);
    return views_.size() != viewCount;
}

bool FractalAnimator::reattachView(FractalViewId viewId, lv_obj_t* parent, int width, int height)
{
    if (viewId == InvalidViewId || !parent || width <= 0 || height <= 0) {
        return false;
    }

    FractalView* view = findViewById(viewId);
    if (!view) {
        return false;
    }

    view->parent = parent;
    view->viewWidth = width;
    view->viewHeight = height;

    if (!view->canvas || !lv_obj_is_valid(view->canvas)) {
        view->canvas = lv_canvas_create(parent);
        lv_obj_set_pos(view->canvas, 0, 0);
        lv_obj_move_to_index(view->canvas, 0);
        lv_obj_clear_flag(view->canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view->canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(view->canvas, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(view->canvas, onCanvasDeleted, LV_EVENT_DELETE, this);
    }
    else if (lv_obj_get_parent(view->canvas) != parent) {
        lv_obj_set_parent(view->canvas, parent);
    }

    if (view->canvas && lv_obj_is_valid(view->canvas)) {
        lv_obj_set_pos(view->canvas, 0, 0);
        lv_obj_move_to_index(view->canvas, 0);
        lv_obj_clear_flag(view->canvas, LV_OBJ_FLAG_HIDDEN);
    }

    const bool resized = updateRenderTargetSize();
    if (resized) {
        syncViewsToRenderer(true);
    }
    syncView(*view, true);
    return true;
}

bool FractalAnimator::updateView(FractalViewId viewId, int width, int height)
{
    if (viewId == InvalidViewId || width <= 0 || height <= 0) {
        return false;
    }

    FractalView* view = findViewById(viewId);
    if (!view) {
        return false;
    }

    view->viewWidth = width;
    view->viewHeight = height;

    const bool resized = updateRenderTargetSize();
    if (resized) {
        syncViewsToRenderer(true);
    }
    syncView(*view, true);
    return true;
}

bool FractalAnimator::isViewAttached(FractalViewId viewId) const
{
    if (viewId == InvalidViewId) {
        return false;
    }

    return std::any_of(views_.begin(), views_.end(), [viewId](const FractalView& view) {
        return view.id == viewId;
    });
}

void FractalAnimator::park()
{
    if (views_.empty()) {
        return;
    }

    for (auto& view : views_) {
        if (view.canvas && lv_obj_is_valid(view.canvas)) {
            lv_obj_del(view.canvas);
        }
        view.canvas = nullptr;
    }

    views_.clear();
    const bool resized = updateRenderTargetSize();
    if (resized) {
        syncViewsToRenderer(true);
    }
}

void FractalAnimator::parkIfParent(lv_obj_t* parent)
{
    if (!parent) {
        return;
    }

    removeViewByParent(parent, true);
}

void FractalAnimator::update()
{
    if (!fractal_) {
        return;
    }

    const bool swapped = fractal_->update();
    const bool renderSizeChanged = updateRenderSizeCache();

    if (!swapped && !renderSizeChanged) {
        return;
    }

    syncViewsToRenderer(renderSizeChanged);
}

void FractalAnimator::advanceToNextFractal()
{
    if (fractal_) {
        fractal_->advanceToNextFractal();
    }
}

FractalAnimator::FractalView* FractalAnimator::findViewByParent(lv_obj_t* parent)
{
    for (auto& view : views_) {
        if (view.parent == parent) {
            return &view;
        }
    }
    return nullptr;
}

FractalAnimator::FractalView* FractalAnimator::findViewById(FractalViewId viewId)
{
    for (auto& view : views_) {
        if (view.id == viewId) {
            return &view;
        }
    }
    return nullptr;
}

void FractalAnimator::removeViewByCanvas(lv_obj_t* canvas, bool deleteCanvas)
{
    if (!canvas) {
        return;
    }

    for (size_t i = 0; i < views_.size(); ++i) {
        if (views_[i].canvas == canvas) {
            lv_obj_t* canvasToDelete = views_[i].canvas;
            views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(i));
            if (deleteCanvas && canvasToDelete && lv_obj_is_valid(canvasToDelete)) {
                lv_obj_del(canvasToDelete);
            }
            const bool resized = updateRenderTargetSize();
            if (resized) {
                syncViewsToRenderer(true);
            }
            return;
        }
    }
}

void FractalAnimator::removeViewById(FractalViewId viewId, bool deleteCanvas)
{
    if (viewId == InvalidViewId) {
        return;
    }

    for (size_t i = 0; i < views_.size(); ++i) {
        if (views_[i].id == viewId) {
            lv_obj_t* canvasToDelete = views_[i].canvas;
            views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(i));
            if (deleteCanvas && canvasToDelete && lv_obj_is_valid(canvasToDelete)) {
                lv_obj_del(canvasToDelete);
            }
            const bool resized = updateRenderTargetSize();
            if (resized) {
                syncViewsToRenderer(true);
            }
            return;
        }
    }
}

void FractalAnimator::removeViewByParent(lv_obj_t* parent, bool deleteCanvas)
{
    if (!parent) {
        return;
    }

    for (size_t i = 0; i < views_.size(); ++i) {
        if (views_[i].parent == parent) {
            lv_obj_t* canvasToDelete = views_[i].canvas;
            views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(i));
            if (deleteCanvas && canvasToDelete && lv_obj_is_valid(canvasToDelete)) {
                lv_obj_del(canvasToDelete);
            }
            const bool resized = updateRenderTargetSize();
            if (resized) {
                syncViewsToRenderer(true);
            }
            return;
        }
    }
}

void FractalAnimator::syncViewsToRenderer(bool updateScale)
{
    if (!fractal_) {
        return;
    }

    bool viewsRemoved = false;
    for (size_t i = 0; i < views_.size();) {
        auto& view = views_[i];
        if (!view.canvas || !lv_obj_is_valid(view.canvas)) {
            views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(i));
            viewsRemoved = true;
            continue;
        }

        syncView(view, updateScale);
        ++i;
    }

    if (viewsRemoved) {
        const bool resized = updateRenderTargetSize();
        if (resized) {
            syncViewsToRenderer(true);
        }
    }
}

void FractalAnimator::syncView(FractalView& view, bool updateScale)
{
    if (!fractal_ || !view.canvas || !lv_obj_is_valid(view.canvas)) {
        return;
    }

    const int renderWidth = fractal_->getRenderWidth();
    const int renderHeight = fractal_->getRenderHeight();

    if (renderWidth <= 0 || renderHeight <= 0) {
        return;
    }

    if (!fractal_->getFrontBuffer()) {
        return;
    }

    lv_canvas_set_buffer(
        view.canvas,
        fractal_->getFrontBuffer(),
        renderWidth,
        renderHeight,
        LV_COLOR_FORMAT_ARGB8888);

    if (updateScale) {
        const int scaleX = (view.viewWidth * 256) / renderWidth;
        const int scaleY = (view.viewHeight * 256) / renderHeight;
        lv_obj_set_style_transform_scale_x(view.canvas, scaleX, 0);
        lv_obj_set_style_transform_scale_y(view.canvas, scaleY, 0);
    }

    lv_obj_invalidate(view.canvas);
}

bool FractalAnimator::updateRenderTargetSize()
{
    int maxWidth = 0;
    int maxHeight = 0;
    for (const auto& view : views_) {
        maxWidth = std::max(maxWidth, view.viewWidth);
        maxHeight = std::max(maxHeight, view.viewHeight);
    }

    if (maxWidth <= 0 || maxHeight <= 0) {
        return false;
    }

    if (!fractal_) {
        fractal_ = std::make_unique<JuliaFractal>(maxWidth, maxHeight);
        targetWidth_ = maxWidth;
        targetHeight_ = maxHeight;
        updateRenderSizeCache();
        return true;
    }

    if (maxWidth == targetWidth_ && maxHeight == targetHeight_) {
        return false;
    }

    targetWidth_ = maxWidth;
    targetHeight_ = maxHeight;
    fractal_->resize(targetWidth_, targetHeight_);
    return updateRenderSizeCache();
}

bool FractalAnimator::updateRenderSizeCache()
{
    if (!fractal_) {
        return false;
    }

    const int renderWidth = fractal_->getRenderWidth();
    const int renderHeight = fractal_->getRenderHeight();
    if (renderWidth == renderWidth_ && renderHeight == renderHeight_) {
        return false;
    }

    renderWidth_ = renderWidth;
    renderHeight_ = renderHeight;
    return true;
}

void FractalAnimator::onCanvasDeleted(lv_event_t* e)
{
    auto* animator = static_cast<FractalAnimator*>(lv_event_get_user_data(e));
    if (!animator) {
        return;
    }

    auto* canvas = static_cast<lv_obj_t*>(lv_event_get_target(e));
    animator->removeViewByCanvas(canvas, false);
}

} // namespace Ui
} // namespace DirtSim
