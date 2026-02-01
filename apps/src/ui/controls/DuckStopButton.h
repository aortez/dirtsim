#pragma once

#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {

class FractalAnimator;

class DuckStopButton {
public:
    DuckStopButton(
        lv_obj_t* parent,
        FractalAnimator& fractalAnimator,
        int width,
        int height,
        const char* labelText = "Stop");
    ~DuckStopButton();

    lv_obj_t* getButton() const { return button_; }

private:
    FractalAnimator& fractalAnimator_;
    lv_obj_t* button_ = nullptr;
    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* duckImage_ = nullptr;
    lv_obj_t* label_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool showLabel_ = true;

    void createButton(lv_obj_t* parent, const char* labelText);
    void updateDuckScale();
};

} // namespace Ui
} // namespace DirtSim
