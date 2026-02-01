#pragma once

#include "ui/rendering/JuliaFractal.h"
#include <cstdint>
#include <lvgl/lvgl.h>
#include <memory>
#include <vector>

namespace DirtSim {
namespace Ui {

class FractalAnimator {
public:
    using FractalViewId = uint64_t;
    static constexpr FractalViewId InvalidViewId = 0;

    FractalAnimator() = default;
    ~FractalAnimator();

    void attachTo(lv_obj_t* parent, int width, int height);
    FractalViewId attachView(lv_obj_t* parent, int width, int height);
    bool detachView(FractalViewId viewId);
    bool reattachView(FractalViewId viewId, lv_obj_t* parent, int width, int height);
    bool updateView(FractalViewId viewId, int width, int height);
    bool isViewAttached(FractalViewId viewId) const;
    void park();
    void parkIfParent(lv_obj_t* parent);
    void update();
    void advanceToNextFractal();

    JuliaFractal* getFractal() const { return fractal_.get(); }

private:
    struct FractalView {
        FractalViewId id = InvalidViewId;
        lv_obj_t* parent = nullptr;
        lv_obj_t* canvas = nullptr;
        int viewWidth = 0;
        int viewHeight = 0;
    };

    FractalView* findViewById(FractalViewId viewId);
    FractalView* findViewByParent(lv_obj_t* parent);
    void removeViewByCanvas(lv_obj_t* canvas, bool deleteCanvas);
    void removeViewById(FractalViewId viewId, bool deleteCanvas);
    void removeViewByParent(lv_obj_t* parent, bool deleteCanvas);
    void syncViewsToRenderer(bool updateScale);
    void syncView(FractalView& view, bool updateScale);
    bool updateRenderTargetSize();
    bool updateRenderSizeCache();
    static void onCanvasDeleted(lv_event_t* e);

    std::unique_ptr<JuliaFractal> fractal_;
    std::vector<FractalView> views_;
    int renderHeight_ = 0;
    int renderWidth_ = 0;
    int targetHeight_ = 0;
    int targetWidth_ = 0;
    FractalViewId nextViewId_ = 1;
};

} // namespace Ui
} // namespace DirtSim
