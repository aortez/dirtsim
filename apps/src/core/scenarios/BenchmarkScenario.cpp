#include "BenchmarkScenario.h"
#include "core/Cell.h"
#include "core/ScenarioConfig.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cstdlib>

namespace DirtSim {

BenchmarkScenario::BenchmarkScenario()
{
    metadata_.name = "Benchmark";
    metadata_.description = "Performance test: 200x200 world with water pool and falling balls";
    metadata_.category = "benchmark";
    metadata_.requiredWidth = 200;
    metadata_.requiredHeight = 200;

    // Initialize with empty config.
    config_ = Config::Benchmark{};
}

const ScenarioMetadata& BenchmarkScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig BenchmarkScenario::getConfig() const
{
    return config_;
}

void BenchmarkScenario::setConfig(const ScenarioConfig& newConfig, World& /*world*/)
{
    // Validate type and update.
    if (std::holds_alternative<Config::Benchmark>(newConfig)) {
        config_ = std::get<Config::Benchmark>(newConfig);
        spdlog::info("BenchmarkScenario: Config updated");
    }
    else {
        spdlog::error("BenchmarkScenario: Invalid config type provided");
    }
}

void BenchmarkScenario::tick(World& /*world*/, double /*deltaTime*/)
{
    // No ongoing behavior needed - just initial setup.
}

void BenchmarkScenario::setup(World& world)
{
    spdlog::info(
        "BenchmarkScenario::setup - initializing {}x{} world",
        world.getData().width,
        world.getData().height);

    // Clear world first.
    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            world.getData().at(x, y) = Cell(); // Reset to empty cell.
        }
    }

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

    // Fill bottom 1/3 with water.
    int waterStartY = world.getData().height - (world.getData().height / 3);
    for (int y = waterStartY; y < world.getData().height - 1; ++y) {
        for (int x = 1; x < world.getData().width - 1; ++x) {
            world.getData().at(x, y).replaceMaterial(Material::EnumType::Water, 1.0);
        }
    }
    spdlog::info(
        "Added water pool (bottom 1/3): rows {}-{}", waterStartY, world.getData().height - 1);

    // Calculate ball diameter as 15% of minimum world dimension.
    uint32_t minDimension = std::min(world.getData().width, world.getData().height);
    uint32_t ballDiameter = static_cast<uint32_t>(minDimension * 0.15);
    uint32_t ballRadius = ballDiameter / 2;

    // Position balls proportionally to world size.
    uint32_t metalBallX = world.getData().width / 5;
    uint32_t metalBallY = world.getData().height / 10;
    addBall(world, metalBallX, metalBallY, ballRadius, Material::EnumType::Metal);
    spdlog::info("Added metal ball at ({}, {}), radius {}", metalBallX, metalBallY, ballRadius);

    uint32_t woodBallX = (4 * world.getData().width) / 5;
    uint32_t woodBallY = world.getData().height / 10;
    addBall(world, woodBallX, woodBallY, ballRadius, Material::EnumType::Wood);
    spdlog::info("Added wood ball at ({}, {}), radius {}", woodBallX, woodBallY, ballRadius);

    // Add random sand particles (5% of world space).
    uint32_t totalCells = world.getData().width * world.getData().height;
    uint32_t sandCellCount = static_cast<uint32_t>(totalCells * 0.05);
    uint32_t sandAdded = 0;

    // Use simple pseudo-random generation for reproducibility.
    std::srand(42); // Fixed seed for consistent benchmarks.

    while (sandAdded < sandCellCount) {
        uint32_t x = 1 + (std::rand() % (world.getData().width - 2));
        uint32_t y = 1 + (std::rand() % (world.getData().height - 2));

        // Only add sand to AIR cells (don't overwrite water, balls, or walls).
        if (world.getData().at(x, y).material_type == Material::EnumType::Air) {
            world.getData().at(x, y).replaceMaterial(Material::EnumType::Sand, 1.0);
            sandAdded++;
        }
    }
    spdlog::info("Added {} random sand particles (5% of {} cells)", sandAdded, totalCells);

    spdlog::info("BenchmarkScenario::setup complete");
}

void BenchmarkScenario::reset(World& world)
{
    spdlog::info("BenchmarkScenario::reset - resetting world");
    setup(world);
}

void BenchmarkScenario::addBall(
    World& world, uint32_t centerX, uint32_t centerY, uint32_t radius, Material::EnumType material)
{
    // Create a circular ball of material.
    for (int y = 0; y < world.getData().height; ++y) {
        for (int x = 0; x < world.getData().width; ++x) {
            // Calculate distance from center.
            int dx = static_cast<int>(x) - static_cast<int>(centerX);
            int dy = static_cast<int>(y) - static_cast<int>(centerY);
            double distance = std::sqrt(dx * dx + dy * dy);

            // If within radius, add material.
            if (distance <= static_cast<double>(radius)) {
                world.getData().at(x, y).replaceMaterial(material, 1.0);
            }
        }
    }
}

} // namespace DirtSim
