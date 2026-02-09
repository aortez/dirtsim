#pragma once

#include <chrono>
#include <cstdint>
#include <lvgl/lvgl.h>
#include <random>
#include <vector>

namespace DirtSim {
namespace Ui {

class Starfield {
public:
    struct SnapshotStar {
        float x = 0.0f;
        float y = 0.0f;
        float speed = 0.0f;
        float twinklePhase = 0.0f;
        float twinkleSpeed = 0.0f;
        uint8_t brightness = 0;
        uint8_t size = 1;
    };

    struct Snapshot {
        int width = 0;
        int height = 0;
        std::vector<SnapshotStar> stars;
        std::mt19937 rng;
    };

    Starfield(lv_obj_t* parent, int width, int height, const Snapshot* snapshot = nullptr);
    ~Starfield();

    void update();
    void resize(int width, int height);
    void setVisible(bool visible);
    bool isVisible() const;

    Snapshot capture() const;

    lv_obj_t* getCanvas() const { return canvas_; }

private:
    struct Star {
        float x = 0.0f;
        float y = 0.0f;
        float speed = 0.0f;
        float twinklePhase = 0.0f;
        float twinkleSpeed = 0.0f;
        uint8_t brightness = 0;
        uint8_t size = 1;
    };

    void drawStars();
    void initStars();
    void maybeResize();
    void resetStar(Star& star, bool randomY);
    void updateStars(float deltaSeconds);

    lv_obj_t* parent_ = nullptr;
    lv_obj_t* canvas_ = nullptr;
    lv_color_t* canvasBuffer_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int starCount_ = 0;
    std::vector<Star> stars_;
    std::mt19937 rng_;
    std::chrono::steady_clock::time_point lastUpdate_;
};

} // namespace Ui
} // namespace DirtSim
