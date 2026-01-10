#pragma once

#include <array>
#include <functional>
#include <lvgl/lvgl.h>

namespace DirtSim {
namespace Ui {

/**
 * @brief Animated "Start Simulation" button featuring a duck and sparkles.
 *
 * Creates an eye-catching button with:
 * - Cute duck character
 * - Animated sparkle particles orbiting the button
 * - Press animation
 * - Starburst effect when pressed (sparkles spin up and fly outward in waves)
 */
class SparklingDuckButton {
public:
    using ClickCallback = std::function<void()>;

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
    static constexpr int NUM_SPARKLES = 32;
    static constexpr int BUTTON_WIDTH = 160;
    static constexpr int BUTTON_HEIGHT = 160;

    // Sparkle animation phases.
    enum class SparklePhase {
        ORBITING,    // Normal orbit around button.
        SPINNING_UP, // Starting from center, accelerating to orbit.
        FLYING_OUT   // Released, flying outward in starburst.
    };

    // Per-sparkle animation state.
    struct SparkleState {
        float radius;                // Current distance from button center.
        float targetRadius;          // Normal orbit radius for this sparkle.
        float angle;                 // Current angle (radians).
        float angularVelocity;       // Current angular velocity.
        float targetAngularVelocity; // Normal orbit angular velocity.
        float radialVelocity;        // Outward velocity when flying.
        SparklePhase phase;          // Current animation phase.
        float opacity;               // Current opacity (for fading).
    };

    // Duck's color palette for "disco duck" effect when pressed.
    static constexpr int NUM_PALETTE_COLORS = 6;
    static constexpr uint32_t DUCK_PALETTE[NUM_PALETTE_COLORS] = {
        0x2E7D32, // Forest green (head).
        0xFFFFFF, // White (neck ring).
        0x8D6E63, // Chestnut brown (breast).
        0x78909C, // Gray-brown (body).
        0xFF7043, // Orange (feet/bill).
        0x5C6BC0  // Purple-blue (wing speculum).
    };

    lv_obj_t* button_ = nullptr;
    lv_obj_t* duckBackground_ = nullptr; // Dark circle behind duck for contrast.
    lv_obj_t* duckImage_ = nullptr;
    std::array<lv_obj_t*, NUM_SPARKLES> sparkles_{};
    std::array<SparkleState, NUM_SPARKLES> sparkleStates_{};

    ClickCallback onClick_;
    uint32_t frameCount_ = 0;
    bool isPressed_ = false;
    int paletteIndex_ = 0;      // Current color in palette cycle.
    int colorCycleCounter_ = 0; // Frames since last color change.

    void createButton(lv_obj_t* parent);
    void createSparkles(lv_obj_t* parent);
    void initSparkleState(int index);
    void updateSparklePositions();
    void updateSparkleOrbiting(int i, lv_coord_t btnX, lv_coord_t btnY);
    void updateSparkleSpinningUp(int i, lv_coord_t btnX, lv_coord_t btnY);
    void updateSparkleFlyingOut(int i, lv_coord_t btnX, lv_coord_t btnY);
    void updateDuckColorCycle();

    static void onButtonClicked(lv_event_t* e);
    static void onButtonPressed(lv_event_t* e);
    static void onButtonReleased(lv_event_t* e);
};

} // namespace Ui
} // namespace DirtSim
