#pragma once

#include "core/MaterialType.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <random>

namespace DirtSim {

class Cell;
class World;

/**
 * Manages the floor drain for the clock scenario.
 *
 * The drain opens in response to water accumulation in the bottom third of the world.
 * Opening size varies (1, 3, 5, 7 cells wide) based on water amount, with hysteresis
 * to prevent rapid flickering. Material in drain cells is sprayed upward and dissipates.
 */
class DrainManager {
public:
    void reset();
    void update(
        World& world,
        double deltaTime,
        double waterAmount,
        std::optional<Material::EnumType> extraDrainMaterial,
        std::mt19937& rng);

    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] int16_t getStartX() const { return startX_; }
    [[nodiscard]] int16_t getEndX() const { return endX_; }

private:
    bool open_ = false;
    int16_t startX_ = 0;
    int16_t endX_ = 0;
    int16_t currentSize_ = 0;
    std::chrono::steady_clock::time_point lastSizeChange_{};

    void updateSize(World& world, double waterAmount);
    void updateCells(
        World& world,
        double deltaTime,
        std::optional<Material::EnumType> extraMaterial,
        std::mt19937& rng);
    void applyGravity(World& world);
    void sprayCell(World& world, Cell& cell, int16_t x, int16_t y);

    static constexpr double kCloseThreshold = 0.2;
    static constexpr double kFullOpenThreshold = 100.0;
    static constexpr int16_t kMaxSize = 7;
    static constexpr int kSizeChangeIntervalMs = 1000;
};

} // namespace DirtSim
