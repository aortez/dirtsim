#include "SandboxScenario.h"
#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/PhysicsSettings.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include "spdlog/spdlog.h"
#include <cmath>

namespace DirtSim {

SandboxScenario::SandboxScenario()
{
    metadata_.name = "Sandbox";
    metadata_.description = "Default sandbox with dirt quadrant and particle streams (no walls)";
    metadata_.category = "sandbox";
    metadata_.requiredWidth = 47;
    metadata_.requiredHeight = 30;

    // Initialize with default config.
    config_.quadrantEnabled = true;
    config_.waterColumnEnabled = true;
    config_.rightThrowEnabled = true;
    config_.rainRate = 0.0;
}

const ScenarioMetadata& SandboxScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig SandboxScenario::getConfig() const
{
    return config_;
}

void SandboxScenario::setConfig(const ScenarioConfig& newConfig, World& world)
{
    // Validate type and update.
    if (std::holds_alternative<Config::Sandbox>(newConfig)) {
        const Config::Sandbox& newSandboxConfig = std::get<Config::Sandbox>(newConfig);

        // Check if water column state changed.
        bool wasWaterEnabled = config_.waterColumnEnabled;
        bool nowWaterEnabled = newSandboxConfig.waterColumnEnabled;

        // Check if quadrant state changed.
        bool wasQuadrantEnabled = config_.quadrantEnabled;
        bool nowQuadrantEnabled = newSandboxConfig.quadrantEnabled;

        // Update config.
        config_ = newSandboxConfig;

        // Apply water column changes immediately.
        if (!wasWaterEnabled && nowWaterEnabled) {
            waterColumnStartTime_ = 0.0;
            addWaterColumn(world);
            spdlog::info("SandboxScenario: Water column enabled and added");
        }
        else if (wasWaterEnabled && !nowWaterEnabled) {
            waterColumnStartTime_ = -1.0;
            clearWaterColumn(world);
            spdlog::info("SandboxScenario: Water column disabled and cleared");
        }

        // Apply quadrant changes immediately.
        if (!wasQuadrantEnabled && nowQuadrantEnabled) {
            addDirtQuadrant(world);
            spdlog::info("SandboxScenario: Dirt quadrant enabled and added");
        }
        else if (wasQuadrantEnabled && !nowQuadrantEnabled) {
            clearDirtQuadrant(world);
            spdlog::info("SandboxScenario: Dirt quadrant disabled and cleared");
        }

        spdlog::info("SandboxScenario: Config updated");
    }
    else {
        spdlog::error("SandboxScenario: Invalid config type provided");
    }
}

void SandboxScenario::setup(World& world)
{
    LOG_INFO(Scenario, "setup - initializing world");

    // Clear world first (cells and trees).
    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell(); // Reset to empty cell.
        }
    }
    world.getOrganismManager().clear();

    // Create boundary walls (no top wall - allows sunlight to illuminate the world).
    for (int x = 0; x < world.getData().width; ++x) {
        world.getData()
            .at(x, world.getData().height - 1)
            .replaceMaterial(Material::EnumType::Wall, 1.0); // Bottom wall.
    }
    for (int y = 0; y < world.getData().height; ++y) {
        world.getData().at(0, y).replaceMaterial(Material::EnumType::Wall, 1.0); // Left wall.
        world.getData()
            .at(world.getData().width - 1, y)
            .replaceMaterial(Material::EnumType::Wall, 1.0); // Right wall.
    }

    // Fill lower-right quadrant if enabled.
    if (config_.quadrantEnabled) {
        addDirtQuadrant(world);
    }

    // Add water column if enabled.
    if (config_.waterColumnEnabled) {
        addWaterColumn(world);
        // Initialize water column start time.
        waterColumnStartTime_ = 0.0;
    }

    LOG_INFO(Scenario, "setup complete");
}

void SandboxScenario::reset(World& world)
{
    LOG_INFO(Scenario, "reset - resetting world and timers");

    // Reset timing state.
    lastSimTime_ = 0.0;
    nextRightThrow_ = 1.0;
    waterColumnStartTime_ = -1.0;

    // Re-run setup to reinitialize world.
    setup(world);
}

void SandboxScenario::tick(World& world, double deltaTime)
{
    const double simTime = lastSimTime_ + deltaTime;

    // Recurring throws from right side every ~0.83 seconds (if enabled).
    const double throwPeriod = 0.83;
    if (config_.rightThrowEnabled && simTime >= nextRightThrow_) {
        throwDirtBalls(world);
        nextRightThrow_ += throwPeriod;
    }

    // Rain drops - time-scale independent, probability-based.
    if (config_.rainRate > 0.0) {
        addRainDrops(world, deltaTime);
    }

    // Water column refill (if enabled).
    if (config_.waterColumnEnabled) {
        // Initialize start time on first call.
        if (waterColumnStartTime_ == 0.0) {
            waterColumnStartTime_ = simTime;
            spdlog::info(
                "Water column starting at time {:.3f}s (will auto-disable after {:.1f}s)",
                waterColumnStartTime_,
                WATER_COLUMN_DURATION);
        }

        // Check for auto-disable timeout.
        if (waterColumnStartTime_ > 0.0) {
            double elapsed = simTime - waterColumnStartTime_;
            if (elapsed >= WATER_COLUMN_DURATION) {
                spdlog::info(
                    "Water column auto-disabling after {:.1f} seconds (elapsed: {:.1f}s)",
                    WATER_COLUMN_DURATION,
                    elapsed);
                config_.waterColumnEnabled = false;
                waterColumnStartTime_ = -1.0;
            }
        }

        // Refill if still enabled.
        if (config_.waterColumnEnabled) {
            refillWaterColumn(world);
        }
    }

    lastSimTime_ = simTime;
}

void SandboxScenario::addWaterColumn(World& world)
{
    // Scale water column dimensions based on world size.
    int columnWidth = std::max(3, std::min(8, static_cast<int>(world.getData().width) / 20));
    int columnHeight = world.getData().height / 3;

    // Add water column on left side.
    for (int y = 0; y < columnHeight && y < world.getData().height; ++y) {
        for (int x = 1; x <= columnWidth && x < world.getData().width; ++x) {
            world.getData().at(x, y).addWater(1.0);
        }
    }
    spdlog::info("Added water column ({} wide × {} tall) on left side", columnWidth, columnHeight);
}

void SandboxScenario::clearWaterColumn(World& world)
{
    // Scale water column dimensions based on world size.
    int columnWidth = std::max(3, std::min(8, static_cast<int>(world.getData().width) / 20));
    int columnHeight = world.getData().height / 3;

    // Clear water from the water column area.
    for (int y = 0; y < columnHeight && y < world.getData().height; ++y) {
        for (int x = 1; x <= columnWidth && x < world.getData().width; ++x) {
            Cell& cell = world.getData().at(x, y);
            if (cell.material_type == Material::EnumType::Water) {
                cell.replaceMaterial(Material::EnumType::Air, 0.0);
            }
        }
    }
    spdlog::info("Cleared water column");
}

void SandboxScenario::addDirtQuadrant(World& world)
{
    // Fill lower-right quadrant with dirt.
    int startX = world.getData().width / 2;
    int startY = world.getData().height / 2;
    for (int y = startY; y < world.getData().height - 1; ++y) {
        for (int x = startX; x < world.getData().width - 1; ++x) {
            world.getData().at(x, y).addDirt(1.0);
        }
    }
    spdlog::info(
        "Added dirt quadrant ({}x{} cells)", world.getData().width / 2, world.getData().height / 2);
}

void SandboxScenario::clearDirtQuadrant(World& world)
{
    // Clear dirt from lower-right quadrant.
    int startX = world.getData().width / 2;
    int startY = world.getData().height / 2;
    for (int y = startY; y < world.getData().height - 1; ++y) {
        for (int x = startX; x < world.getData().width - 1; ++x) {
            Cell& cell = world.getData().at(x, y);
            if (cell.material_type == Material::EnumType::Dirt) {
                cell.replaceMaterial(Material::EnumType::Air, 0.0);
            }
        }
    }
    spdlog::info("Cleared dirt quadrant");
}

void SandboxScenario::refillWaterColumn(World& world)
{
    // Scale water column dimensions based on world size.
    int columnWidth = std::max(3, std::min(8, static_cast<int>(world.getData().width) / 20));
    int columnHeight = world.getData().height / 3;

    // Refill any empty or water cells in the water column area.
    for (int y = 0; y < columnHeight && y < world.getData().height; ++y) {
        for (int x = 1; x <= columnWidth && x < world.getData().width; ++x) {
            Cell& cell = world.getData().at(x, y);
            // Only refill if cell is air or water, and not already full.
            if ((cell.material_type == Material::EnumType::Air
                 || cell.material_type == Material::EnumType::Water)
                && !cell.isFull()) {
                cell.addWater(1.0 - cell.fill_ratio);
            }
        }
    }
}

void SandboxScenario::addRainDrops(World& world, double deltaTime)
{
    // Normalize rainRate from [0, 10] to [0, 1].
    double normalized_rate = config_.rainRate / 10.0;
    if (normalized_rate <= 0.0) {
        return;
    }

    // Scale with world width so larger worlds get proportionally more rain.
    double widthScale = world.getData().width / 20.0;

    // Drop count scales linearly with rainRate (more rain = more drops).
    const double baseDropsPerSecond = 3.0; // Tunable drop frequency.
    double expectedDrops = config_.rainRate * baseDropsPerSecond * deltaTime * widthScale;

    // Drop radius scales quadratically with rainRate AND proportionally with world width.
    // Low rate → tiny misting drops, high rate → large concentrated drops.
    // Larger worlds → proportionally larger drops (keeps visual consistency).
    const double scalar_factor = 5.0;
    double baseRadius = normalized_rate * normalized_rate * scalar_factor;
    double meanRadius = baseRadius * widthScale;

    // Total water rate scales quadratically (matches drop size scaling).
    // rainRate=1 → 0.01× water, rainRate=5 → 0.25× water, rainRate=10 → 1.0× water.
    const double baseWaterConstant = 50.0; // Tunable wetness factor.
    double targetWaterRate = normalized_rate * normalized_rate * baseWaterConstant * widthScale;

    // Variance: normal distribution with rate-dependent std deviation.
    // More variance at high rates (20% to 50% std).
    double stdRadius = meanRadius * (0.2 + normalized_rate * 0.3);
    std::normal_distribution<double> radiusDist(meanRadius, stdRadius);

    // Use Poisson distribution for realistic randomness in drop count.
    std::poisson_distribution<int> poissonDrops(std::max(0.0, expectedDrops));
    int numDrops = poissonDrops(rng_);

    if (numDrops == 0) {
        return;
    }

    // Calculate fill amount to achieve target water rate.
    // fill = targetWater / (drops × area).
    double meanDropArea = M_PI * meanRadius * meanRadius;
    double fillAmount = (targetWaterRate * deltaTime) / (numDrops * meanDropArea);
    fillAmount = std::clamp(fillAmount, 0.01, 1.0);

    spdlog::debug(
        "Adding {} rain drops (rate: {:.1f}, meanRadius: {:.2f}, fill: {:.2f}, deltaTime: {:.3f}s)",
        numDrops,
        config_.rainRate,
        meanRadius,
        fillAmount,
        deltaTime);

    // Uniform distributions for position.
    std::uniform_int_distribution<int> xDist(1, world.getData().width - 2);

    // Top 15% of world for vertical spawning (minimum 3 cells).
    int maxY = std::max(3, static_cast<int>(world.getData().height * 0.15));
    std::uniform_int_distribution<int> yDist(1, maxY);

    // Spawn drops with varying sizes.
    for (int i = 0; i < numDrops; i++) {
        int x = xDist(rng_);
        int y = yDist(rng_);

        // Sample radius from normal distribution (each drop varies).
        double dropRadius = radiusDist(rng_);
        dropRadius = std::max(0.01, dropRadius); // Very small minimum for misting.

        // Use the calculated fill amount for all drops.
        spawnWaterDrop(world, x, y, dropRadius, fillAmount);
    }
}

void SandboxScenario::spawnWaterDrop(
    World& world, int centerX, int centerY, double radius, double fillAmount)
{
    // Spawn a circular water drop with specified fill amount.
    // Tiny drops (radius < 1) create misting effect with partial fills.
    // Large drops (radius ≥ 1) fill cells completely.

    // Calculate bounding box (only scan area that could be affected).
    int radiusInt = static_cast<int>(std::ceil(radius));
    int minX = std::max(0, centerX - radiusInt);
    int maxX = std::min(centerX + radiusInt, static_cast<int>(world.getData().width) - 1);
    int minY = std::max(0, centerY - radiusInt);
    int maxY = std::min(centerY + radiusInt, static_cast<int>(world.getData().height) - 1);

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            // Calculate distance from center.
            int dx = x - centerX;
            int dy = y - centerY;
            double distance = std::sqrt(dx * dx + dy * dy);

            // If within radius, add water with specified fill amount.
            if (distance <= radius) {
                world.getData().at(x, y).addWater(fillAmount);
            }
        }
    }
}

void SandboxScenario::throwDirtBalls(World& world)
{
    spdlog::debug("Adding right periodic throw");
    int rightX = world.getData().width - 3;
    int centerY = world.getData().height / 2 - 2;
    if (world.getData().inBounds(rightX, centerY)) {
        Cell& cell = world.getData().at(rightX, centerY);
        cell.addDirtWithVelocity(1.0, Vector2d{ -10, -10 });
    }
}

} // namespace DirtSim
