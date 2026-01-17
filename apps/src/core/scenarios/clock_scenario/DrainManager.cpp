#include "DrainManager.h"

#include "core/Cell.h"
#include "core/FragmentationParams.h"
#include "core/MaterialType.h"
#include "core/Vector2.h"
#include "core/World.h"
#include "core/WorldCollisionCalculator.h"
#include "core/WorldData.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {

void DrainManager::reset()
{
    open_ = false;
    startX_ = 0;
    endX_ = 0;
    currentSize_ = 0;
    lastSizeChange_ = {};
}

void DrainManager::update(
    World& world,
    double deltaTime,
    double waterAmount,
    std::optional<Material::EnumType> extraDrainMaterial,
    std::mt19937& rng)
{
    const WorldData& data = world.getData();
    if (data.height < 3 || data.width < 5) {
        return;
    }

    updateSize(world, waterAmount);
    updateCells(world, deltaTime, extraDrainMaterial, rng);

    if (open_) {
        applyGravity(world);
    }
}

void DrainManager::updateSize(World& world, double waterAmount)
{
    WorldData& data = world.getData();

    // Calculate target drain size based on water level (odd numbers only: 3, 5, 7).
    // Size 1 is only used as an animation transition step, not a sustained state.
    int16_t targetSize = 0;
    if (waterAmount >= kFullOpenThreshold) {
        targetSize = kMaxSize;
    }
    else if (waterAmount >= kCloseThreshold) {
        // Linear interpolation from 3 to kMaxSize, quantized to odd numbers.
        double t = (waterAmount - kCloseThreshold) / (kFullOpenThreshold - kCloseThreshold);
        uint32_t continuousSize = 3 + static_cast<uint32_t>(t * (kMaxSize - 3));

        // Round to nearest odd number (3, 5, 7).
        if (continuousSize % 2 == 0) {
            targetSize = static_cast<int16_t>(continuousSize - 1);
        }
        else {
            targetSize = static_cast<int16_t>(continuousSize);
        }
    }

    // If there's any water on the bottom playable row, ensure drain opens at least one cell.
    if (targetSize == 0) {
        int bottomRow = data.height - 2;
        for (int x = 1; x < data.width - 1; ++x) {
            if (data.at(x, bottomRow).material_type == Material::EnumType::Water) {
                targetSize = 1;
                break;
            }
        }
    }

    // Hysteresis: only change drain size one step per second.
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSizeChange_).count();

    int16_t actualSize = currentSize_;
    if (targetSize != currentSize_ && elapsed >= kSizeChangeIntervalMs) {
        // Step one size at a time (0 <-> 1 <-> 3 <-> 5 <-> 7).
        if (targetSize > currentSize_) {
            actualSize = (currentSize_ == 0) ? 1 : static_cast<int16_t>(currentSize_ + 2);
        }
        else {
            actualSize = (currentSize_ == 1) ? 0 : static_cast<int16_t>(currentSize_ - 2);
        }
        currentSize_ = actualSize;
        lastSizeChange_ = now;
    }

    int centerX = data.width / 2;
    int drainY = data.height - 1;

    // Calculate new drain bounds based on actual size (centered).
    int halfDrain = actualSize / 2;
    int newStartX = (actualSize > 0 && centerX > halfDrain) ? centerX - halfDrain : centerX;
    int newEndX = (actualSize > 0) ? std::min(newStartX + actualSize - 1, data.width - 2) : 0;

    if (newStartX < 1) {
        newStartX = 1;
    }

    bool wasOpen = open_;
    int oldStartX = startX_;
    int oldEndX = endX_;

    open_ = (actualSize > 0);
    startX_ = static_cast<int16_t>(newStartX);
    endX_ = static_cast<int16_t>(newEndX);

    // Update drain cells if size changed.
    if (!wasOpen && !open_) {
        return;
    }

    // Restore wall on cells that are no longer in the drain.
    if (wasOpen) {
        for (int x = oldStartX; x <= oldEndX; ++x) {
            bool stillOpen = open_ && x >= newStartX && x <= newEndX;
            if (!stillOpen) {
                world.replaceMaterialAtCell(
                    { static_cast<int16_t>(x), static_cast<int16_t>(drainY) },
                    Material::EnumType::Wall);
            }
        }
    }

    // Ensure drain cells are clear.
    if (open_) {
        for (int x = newStartX; x <= newEndX; ++x) {
            Cell& cell = data.at(x, drainY);
            if (cell.material_type == Material::EnumType::Wall) {
                cell = Cell();
            }
        }
    }

    // Log significant changes.
    if (!wasOpen && open_) {
        spdlog::info(
            "DrainManager: Drain opened (size: {}, water: {:.1f})", actualSize, waterAmount);
    }
    else if (wasOpen && !open_) {
        spdlog::info("DrainManager: Drain closed (water: {:.1f})", waterAmount);
    }
}

void DrainManager::updateCells(
    World& world,
    double deltaTime,
    std::optional<Material::EnumType> extraMaterial,
    std::mt19937& rng)
{
    if (!open_) {
        return;
    }

    WorldData& data = world.getData();
    int drainY = data.height - 1;
    int16_t centerX = static_cast<int16_t>((startX_ + endX_) / 2);
    std::uniform_real_distribution<double> uniformDist(0.0, 1.0);

    for (int16_t x = startX_; x <= endX_; ++x) {
        Cell& cell = data.at(x, drainY);

        // Extra material (e.g., melting digits) converts to water and sprays.
        if (extraMaterial && cell.material_type == *extraMaterial && cell.com.y > 0.0) {
            cell.replaceMaterial(Material::EnumType::Water, cell.fill_ratio);
            sprayCell(world, cell, x, static_cast<int16_t>(drainY));
            continue;
        }

        if (cell.material_type != Material::EnumType::Water) {
            continue;
        }

        if (cell.com.y <= 0.0) {
            continue;
        }

        // Center cell: chance to spray dramatically.
        if (x == centerX && cell.fill_ratio > 0.5 && uniformDist(rng) < 0.7) {
            sprayCell(world, cell, x, static_cast<int16_t>(drainY));
            continue;
        }

        // All drain cells dissipate.
        cell.fill_ratio -= (deltaTime * 10);
        if (cell.fill_ratio <= 0.0) {
            cell = Cell();
        }
    }
}

void DrainManager::applyGravity(World& world)
{
    WorldData& data = world.getData();
    int drainY = data.height - 1;
    double drainCenterX = static_cast<double>(startX_ + endX_) / 2.0;
    double drainCenterY = static_cast<double>(drainY);

    constexpr double kDrainGravity = 1.0;

    // Apply global gravity-like pull toward drain for all water.
    for (int y = 1; y < data.height - 1; ++y) {
        for (int x = 1; x < data.width - 1; ++x) {
            Cell& cell = data.at(x, y);
            if (cell.material_type != Material::EnumType::Water) {
                continue;
            }

            double dx = drainCenterX - static_cast<double>(x);
            double dy = drainCenterY - static_cast<double>(y);
            double dist = std::sqrt(dx * dx + dy * dy);

            if (dist > 0.5) {
                dx /= dist;
                dy /= dist;
                cell.addPendingForce(Vector2d{ dx * kDrainGravity, dy * kDrainGravity });
            }
        }
    }

    // Apply stronger suction force to water on the bottom playable row.
    int bottomRow = drainY - 1;
    double maxDistance = static_cast<double>(data.width) / 2.0;
    constexpr double kMaxForce = 5.0;

    for (int x = 1; x < data.width - 1; ++x) {
        Cell& cell = data.at(x, bottomRow);
        if (cell.material_type != Material::EnumType::Water) {
            continue;
        }

        double cellX = static_cast<double>(x);
        double distance = std::abs(cellX - drainCenterX);
        double strength = 1.0 - 0.9 * std::min(distance / maxDistance, 1.0);
        double forceMagnitude = kMaxForce * strength;

        bool overDrain = (x >= startX_ && x <= endX_);
        double downwardForce = overDrain ? kMaxForce : 0.0;

        double horizontalForce;
        if (overDrain) {
            horizontalForce = -cell.velocity.x * forceMagnitude;
        }
        else {
            double direction = (cellX < drainCenterX) ? 1.0 : -1.0;
            if (std::abs(cellX - drainCenterX) < 0.5) {
                direction = 0.0;
            }
            horizontalForce = direction * forceMagnitude;
        }

        cell.addPendingForce(Vector2d{ horizontalForce, downwardForce });
    }
}

void DrainManager::sprayCell(World& world, Cell& cell, int16_t x, int16_t y)
{
    if (cell.fill_ratio < World::MIN_MATTER_THRESHOLD) {
        cell = Cell();
        return;
    }

    static const FragmentationParams kDrainFragParams{
        .radial_bias = 0.2,
        .min_arc = M_PI / 3.0,
        .max_arc = M_PI / 2.0,
        .edge_speed_factor = 1.2,
        .base_speed = 50.0,
        .spray_fraction = 1.0,
    };

    Vector2d sprayDirection(0.0, -1.0);
    constexpr int kNumFrags = 5;
    constexpr double kArcWidth = M_PI / 2.0;

    world.getCollisionCalculator().fragmentSingleCell(
        world, cell, x, y, x, y, sprayDirection, kNumFrags, kArcWidth, kDrainFragParams);

    cell = Cell();
}

} // namespace DirtSim
