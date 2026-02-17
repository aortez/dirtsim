#include "DuckTrainingScenario.h"

#include "core/Cell.h"
#include "core/ColorNames.h"
#include "core/LightManager.h"
#include "core/LightTypes.h"
#include "core/World.h"
#include "core/WorldData.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace DirtSim {

namespace {

constexpr uint8_t kMaxObstacleCount = 3;

} // namespace

DuckTrainingScenario::DuckTrainingScenario() : rng_(std::random_device{}())
{
    metadata_.name = "Duck Training";
    metadata_.description = "Obstacle course based on the clock duck event";
    metadata_.category = "training";
    metadata_.requiredWidth = 60;
    metadata_.requiredHeight = 16;
}

const ScenarioMetadata& DuckTrainingScenario::getMetadata() const
{
    return metadata_;
}

ScenarioConfig DuckTrainingScenario::getConfig() const
{
    return config_;
}

void DuckTrainingScenario::setConfig(const ScenarioConfig& newConfig, World& world)
{
    if (!std::holds_alternative<Config::DuckTraining>(newConfig)) {
        spdlog::error("DuckTrainingScenario: Invalid config type provided");
        return;
    }

    config_ = std::get<Config::DuckTraining>(newConfig);
    config_.obstacleCount =
        std::clamp(config_.obstacleCount, static_cast<uint8_t>(0), kMaxObstacleCount);

    rebuildWorld(world);
}

void DuckTrainingScenario::setup(World& world)
{
    spdlog::info("DuckTrainingScenario::setup - building obstacle course");
    rebuildWorld(world);
}

void DuckTrainingScenario::reset(World& world)
{
    spdlog::info("DuckTrainingScenario::reset");
    rebuildWorld(world);
}

void DuckTrainingScenario::tick(World& world, double /*deltaTime*/)
{
    redrawWalls(world);
}

void DuckTrainingScenario::rebuildWorld(World& world)
{
    WorldData& data = world.getData();

    world.getLightManager().clear();

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            data.at(x, y) = Cell();
        }
    }

    rng_.seed(config_.obstacleSeed);
    obstacleManager_.clearAll(world);
    spawnObstacles(world);
    redrawWalls(world);

    world.getLightManager().addLight(
        PointLight{ .position = Vector2d{ static_cast<double>(data.width - 2), 2.0 },
                    .color = ColorNames::torchOrange(),
                    .intensity = 0.15f,
                    .radius = 15.0f,
                    .attenuation = 0.05f });

    world.getLightManager().addLight(
        PointLight{ .position = Vector2d{ 2.0, 2.0 },
                    .color = ColorNames::torchOrange(),
                    .intensity = 0.15f,
                    .radius = 15.0f,
                    .attenuation = 0.05f });
}

void DuckTrainingScenario::spawnObstacles(World& world)
{
    const uint8_t desired = std::min(config_.obstacleCount, kMaxObstacleCount);
    uint8_t spawned = 0;
    int attempts = 0;
    while (spawned < desired && attempts < 50) {
        attempts++;
        if (obstacleManager_.spawnObstacle(world, rng_, uniformDist_)) {
            spawned++;
        }
    }

    spdlog::info("DuckTrainingScenario: Spawned {}/{} obstacles", spawned, desired);
}

std::vector<DuckTrainingScenario::WallSpec> DuckTrainingScenario::generateWallSpecs(
    const WorldData& data) const
{
    int16_t width = data.width;
    int16_t height = data.height;

    std::vector<WallSpec> walls;
    walls.reserve(2 * (width + height) + 20);

    for (int16_t x = 0; x < width; ++x) {
        walls.push_back({ x, 0, Material::EnumType::Wood });
    }

    for (int16_t x = 0; x < width; ++x) {
        if (obstacleManager_.isPitAt(static_cast<uint32_t>(x))) {
            continue;
        }
        walls.push_back({ x, static_cast<int16_t>(height - 1), Material::EnumType::Dirt });
    }

    for (int16_t y = 0; y < height; ++y) {
        walls.push_back({ 0, y, Material::EnumType::Wood });
    }

    for (int16_t y = 0; y < height; ++y) {
        walls.push_back({ static_cast<int16_t>(width - 1), y, Material::EnumType::Wood });
    }

    if (height > 2) {
        for (int16_t x = 0; x < width; ++x) {
            if (obstacleManager_.isHurdleAt(static_cast<uint32_t>(x))) {
                walls.push_back({ x, static_cast<int16_t>(height - 2), Material::EnumType::Wall });
            }
        }
    }

    return walls;
}

void DuckTrainingScenario::applyWalls(World& world, const std::vector<WallSpec>& walls)
{
    for (const auto& wall : walls) {
        world.replaceMaterialAtCell({ wall.x, wall.y }, Material::EnumType::Wall);
        world.getData().at(wall.x, wall.y).render_as = static_cast<int8_t>(wall.renderAs);
    }
}

void DuckTrainingScenario::redrawWalls(World& world)
{
    const WorldData& data = world.getData();
    const std::vector<WallSpec> walls = generateWallSpecs(data);
    applyWalls(world, walls);

    const int16_t height = data.height;
    for (int16_t x = 0; x < data.width; ++x) {
        if (!obstacleManager_.isPitAt(static_cast<uint32_t>(x))) {
            continue;
        }
        Cell& cell = world.getData().at(x, height - 1);
        if (cell.material_type == Material::EnumType::Wall) {
            cell = Cell();
        }
    }
}

} // namespace DirtSim
