#pragma once

#include <lvgl/lvgl.h>
#include <array>
#include <functional>

namespace DirtSim {
namespace Ui {

/**
 * @brief Animated "Start Simulation" button featuring a duck and sparkles.
 *
 * Creates an eye-catching button with:
 * - Cute duck character (text-based)
 * - Animated sparkle particles orbiting the button
 * - Gradient-like background with glow effect
 * - Hover/press animations
 */
class SparklingDuckButton {
public:
    using ClickCallback = std::function<void()>;

    /**
     * @brief Create the sparkle-duck button.
     * @param parent Parent LVGL object.
     * @param onClick Callback when button is clicked.
     */
    SparklingDuckButton(lv_obj_t* parent, ClickCallback onClick);

    ~SparklingDuckButton();

    /**
     * @brief Update animations (call each frame).
     */
    void update();

    /**
     * @brief Get the underlying LVGL button object.
     */
    lv_obj_t* getButton() { return button_; }

private:
    static constexpr int NUM_SPARKLES = 8;
    // Bigger button to showcase the duck.
    static constexpr int BUTTON_WIDTH = 160;
    static constexpr int BUTTON_HEIGHT = 160;

    lv_obj_t* button_ = nullptr;
    lv_obj_t* duckImage_ = nullptr;
    std::array<lv_obj_t*, NUM_SPARKLES> sparkles_{};

    ClickCallback onClick_;
    uint32_t frameCount_ = 0;

    void createButton(lv_obj_t* parent);
    void createSparkles(lv_obj_t* parent);
    void updateSparklePositions();

    static void onButtonClicked(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
