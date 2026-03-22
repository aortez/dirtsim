#pragma once

#include "core/MaterialType.h"
#include <chrono>
#include <cstdint>
#include <optional>

namespace DirtSim {

class World;

/**
 * Manages the floor drain for the clock scenario.
 *
 * The drain opens in response to water accumulation in the bottom third of the world.
 * Opening size varies (1, 3, 5, 7 cells wide) based on water amount, with hysteresis
 * to prevent rapid flickering. Material in drain cells is converted into drained bulk water
 * and dissipates while the opening is active.
 */
class DrainManager {
public:
    void reset();
    void update(
        World& world,
        double deltaTime,
        double waterAmount,
        std::optional<Material::EnumType> extraDrainMaterial);

    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] int16_t getStartX() const { return startX_; }
    [[nodiscard]] int16_t getEndX() const { return endX_; }

private:
    bool open_ = false;
    int16_t startX_ = 0;
    int16_t endX_ = 0;
    int16_t currentSize_ = 0;
    std::chrono::steady_clock::time_point lastSizeChange_{};

    bool hasBulkWaterInGuideArea(const World& world) const;
    void updateSize(World& world, double waterAmount);
    void updateCells(
        World& world, double deltaTime, std::optional<Material::EnumType> extraMaterial);
    void applyGravity(World& world);

    static constexpr double kCloseThreshold = 0.2;
    static constexpr double kFullOpenThreshold = 100.0;
    static constexpr int16_t kMaxSize = 7;
    static constexpr int kSizeChangeIntervalMs = 1000;
};

} // namespace DirtSim
