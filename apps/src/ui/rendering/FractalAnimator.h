#pragma once

#include "ui/rendering/JuliaFractal.h"
#include <lvgl/lvgl.h>
#include <memory>

namespace DirtSim {
namespace Ui {

class FractalAnimator {
public:
    FractalAnimator() = default;
    ~FractalAnimator();

    void attachTo(lv_obj_t* parent, int width, int height);
    void park();
    void parkIfParent(lv_obj_t* parent);
    void update();
    void resize(int width, int height);
    void advanceToNextFractal();

    JuliaFractal* getFractal() const { return fractal_.get(); }
    lv_obj_t* getCanvas() const;
    lv_obj_t* getParent() const { return parent_; }

private:
    std::unique_ptr<JuliaFractal> fractal_;
    lv_obj_t* parent_ = nullptr;
};

} // namespace Ui
} // namespace DirtSim
