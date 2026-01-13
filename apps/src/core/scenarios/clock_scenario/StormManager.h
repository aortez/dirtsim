#pragma once

#include "core/ColorNames.h"
#include <chrono>
#include <random>

namespace DirtSim {

class WorldLightCalculator;

/**
 * Manages storm lighting effects for the clock scenario.
 *
 * Lightning is triggered based on storm intensity (water in top third of world).
 * Flashes use a multi-stroke pattern for realism: 2-5 rapid strokes with
 * instant rise, brief peak, and exponential decay.
 */
class StormManager {
public:
    void reset();
    void update(
        WorldLightCalculator& lightCalc,
        double deltaTime,
        double stormIntensity,
        std::mt19937& rng);

    [[nodiscard]] bool isStriking() const { return striking_; }
    [[nodiscard]] float getCurrentFlashIntensity() const { return currentFlashIntensity_; }

private:
    enum class State { Dormant, Striking };

    State state_ = State::Dormant;
    bool striking_ = false;
    float currentFlashIntensity_ = 0.0f;

    // Timing for next strike (Dormant state).
    std::chrono::steady_clock::time_point nextStrikeTime_{};

    // Multi-stroke state (Striking state).
    int totalStrokes_ = 0;
    int currentStroke_ = 0;
    std::chrono::steady_clock::time_point strokeStartTime_{};
    std::chrono::steady_clock::time_point nextStrokeTime_{};

    void enterDormant(double stormIntensity, std::mt19937& rng);
    void enterStriking(std::mt19937& rng);
    void updateDormant(double stormIntensity, std::mt19937& rng);
    void updateStriking(WorldLightCalculator& lightCalc, std::mt19937& rng);
    void applyFlash(WorldLightCalculator& lightCalc, float intensity);

    // Flash envelope: instant rise, brief peak, exponential decay.
    static float calculateStrokeIntensity(double elapsedMs);

    // Timing constants.
    static constexpr double kMinStrikeIntervalMs = 3000.0;
    static constexpr double kMaxStrikeIntervalMs = 12000.0;
    static constexpr int kMinStrokes = 2;
    static constexpr int kMaxStrokes = 5;
    static constexpr double kMinStrokeGapMs = 30.0;
    static constexpr double kMaxStrokeGapMs = 80.0;
    static constexpr double kStrokePeakMs = 20.0;
    static constexpr double kStrokeDecayMs = 150.0;
};

} // namespace DirtSim
