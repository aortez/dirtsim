#include "FractalAnimator.h"
#include "ui/rendering/JuliaFractal.h"

namespace DirtSim {
namespace Ui {

FractalAnimator::~FractalAnimator() = default;

void FractalAnimator::attachTo(lv_obj_t* parent, int width, int height)
{
    if (!parent || width <= 0 || height <= 0) {
        return;
    }

    if (!fractal_) {
        fractal_ = std::make_unique<JuliaFractal>(parent, width, height);
    }
    else {
        lv_obj_t* canvas = fractal_->getCanvas();
        if (canvas && lv_obj_get_parent(canvas) != parent) {
            lv_obj_set_parent(canvas, parent);
        }
        fractal_->resize(width, height);
    }

    parent_ = parent;

    if (lv_obj_t* canvas = getCanvas()) {
        lv_obj_set_pos(canvas, 0, 0);
        lv_obj_move_to_index(canvas, 0);
        lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    }
}

void FractalAnimator::park()
{
    if (!fractal_) {
        return;
    }

    if (lv_obj_t* canvas = getCanvas()) {
        lv_obj_set_parent(canvas, lv_layer_sys());
        lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    }
    parent_ = nullptr;
}

void FractalAnimator::parkIfParent(lv_obj_t* parent)
{
    if (parent_ != parent) {
        return;
    }
    park();
}

void FractalAnimator::update()
{
    if (fractal_) {
        fractal_->update();
    }
}

void FractalAnimator::resize(int width, int height)
{
    if (!fractal_ || width <= 0 || height <= 0) {
        return;
    }
    fractal_->resize(width, height);
}

void FractalAnimator::advanceToNextFractal()
{
    if (fractal_) {
        fractal_->advanceToNextFractal();
    }
}

lv_obj_t* FractalAnimator::getCanvas() const
{
    return fractal_ ? fractal_->getCanvas() : nullptr;
}

} // namespace Ui
} // namespace DirtSim
