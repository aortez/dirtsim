#pragma once

#include "core/RenderMessage.h"

#include <cstdint>
#include <functional>
#include <vector>

typedef struct _lv_obj_t lv_obj_t;

namespace DirtSim {
namespace Ui {

/**
 * Renders pre-rendered video frames (e.g. NES RGB565) to an LVGL canvas at native
 * resolution, letting LVGL transform scaling handle the upscale to the display container.
 */
class VideoSurface {
public:
    using CanvasCreatedCallback = std::function<void(lv_obj_t* canvas)>;

    VideoSurface() = default;
    ~VideoSurface();

    void setCanvasCreatedCallback(CanvasCreatedCallback callback);
    void present(const ScenarioVideoFrame& frame, lv_obj_t* parent);
    void cleanup();

    lv_obj_t* getCanvas() const;
    uint32_t getCanvasWidth() const;
    uint32_t getCanvasHeight() const;
    const uint8_t* getCanvasBuffer() const;

private:
    void initializeCanvas(lv_obj_t* parent, uint16_t frameWidth, uint16_t frameHeight);
    static uint32_t rgb565ToArgb8888(uint16_t rgb565);

    CanvasCreatedCallback canvasCreatedCallback_;
    lv_obj_t* canvas_ = nullptr;
    std::vector<uint8_t> canvasBuffer_;
    uint16_t canvasWidth_ = 0;
    uint16_t canvasHeight_ = 0;
    int32_t lastContainerWidth_ = 0;
    int32_t lastContainerHeight_ = 0;
};

} // namespace Ui
} // namespace DirtSim
