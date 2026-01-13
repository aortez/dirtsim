#pragma once

#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/Duck.h"
#include "core/organisms/DuckBrain.h"
#include "core/organisms/DuckSensoryData.h"
#include "core/organisms/OrganismManager.h"
#include <gtest/gtest.h>
#include <memory>
#include <spdlog/spdlog.h>

namespace DirtSim::Test {

/**
 * Test brain that allows explicit control of duck actions.
 * Supports independent control of movement and jump for air steering tests.
 */
class TestDuckBrain : public DuckBrain {
public:
    void think(Duck& duck, const DuckSensoryData& sensory, double deltaTime) override
    {
        (void)sensory;
        (void)deltaTime;

        // If using direct input mode, apply it directly.
        if (use_direct_input_) {
            duck.setInput(direct_input_);
            // Clear jump after one frame (edge-triggered).
            direct_input_.jump = false;
            return;
        }

        // Legacy action-based mode.
        switch (current_action_) {
            case DuckAction::RUN_LEFT:
                duck.setInput({ .move = { -1.0f, 0.0f }, .jump = false });
                break;
            case DuckAction::RUN_RIGHT:
                duck.setInput({ .move = { 1.0f, 0.0f }, .jump = false });
                break;
            case DuckAction::JUMP:
                duck.setInput({ .move = {}, .jump = true });
                break;
            case DuckAction::WAIT:
            default:
                duck.setInput({ .move = {}, .jump = false });
                break;
        }
    }

    void setAction(DuckAction action) { current_action_ = action; }

    // Direct input control for combined movement + jump.
    void setDirectInput(Vector2f move, bool jump)
    {
        use_direct_input_ = true;
        direct_input_ = { .move = move, .jump = jump };
    }

    void setMove(Vector2f move)
    {
        use_direct_input_ = true;
        direct_input_.move = move;
    }

    void triggerJump()
    {
        use_direct_input_ = true;
        direct_input_.jump = true;
    }

    void clearDirectInput()
    {
        use_direct_input_ = false;
        direct_input_ = { .move = {}, .jump = false };
    }

private:
    bool use_direct_input_ = false;
    DuckInput direct_input_ = { .move = {}, .jump = false };
};

/**
 * Helper struct for common duck test setup.
 * Creates a flat world with floor, spawns duck, and settles it.
 */
struct DuckTestSetup {
    std::unique_ptr<World> world;
    Duck* duck = nullptr;
    TestDuckBrain* brain = nullptr;
    OrganismId duck_id = INVALID_ORGANISM_ID;

    /**
     * Create a flat world with walls and floor, spawn duck, and let it settle.
     *
     * @param width World width.
     * @param height World height (floor at height-1).
     * @param duck_x Duck spawn X position.
     * @param duck_y Duck spawn Y position (typically height-2 for floor level).
     * @param settle_frames Frames to run for settling (default 20).
     */
    static DuckTestSetup create(
        int width, int height, int duck_x, int duck_y, int settle_frames = 20)
    {
        DuckTestSetup setup;

        // Create world.
        setup.world = std::make_unique<World>(width, height);

        // Clear interior to air.
        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {
                setup.world->getData().at(x, y).replaceMaterial(Material::EnumType::Air, 0.0);
            }
        }

        // Ensure floor (bottom row).
        for (int x = 0; x < width; ++x) {
            setup.world->getData().at(x, height - 1).replaceMaterial(Material::EnumType::Wall, 1.0);
        }

        // Create duck with test brain.
        auto brain_ptr = std::make_unique<TestDuckBrain>();
        setup.brain = brain_ptr.get();

        OrganismManager& manager = setup.world->getOrganismManager();
        setup.duck_id = manager.createDuck(*setup.world, duck_x, duck_y, std::move(brain_ptr));
        setup.duck = manager.getDuck(setup.duck_id);

        // Let duck settle.
        setup.brain->setAction(DuckAction::WAIT);
        for (int i = 0; i < settle_frames; ++i) {
            setup.world->advanceTime(0.016);
        }

        return setup;
    }

    // Get duck's current velocity from its cell.
    Vector2d getVelocity() const
    {
        Vector2i pos = duck->getAnchorCell();
        const Cell& cell = world->getData().at(pos.x, pos.y);
        return cell.velocity;
    }

    // Advance simulation by one frame.
    void advance(double dt = 0.016) { world->advanceTime(dt); }

    // Advance simulation by N frames.
    void advanceFrames(int frames, double dt = 0.016)
    {
        for (int i = 0; i < frames; ++i) {
            world->advanceTime(dt);
        }
    }
};

/**
 * Helper to create a world with a cliff.
 *
 * Layout (width x 10):
 *   Row 0: WALL border (ceiling)
 *   Row 1-7: AIR
 *   Row 8: WALL floor from x=0 to cliff_start-1, AIR from cliff_start to cliff_end,
 *          WALL from cliff_end+1 to width-1
 *   Row 9: WALL border (bottom)
 */
inline std::unique_ptr<World> createCliffWorld(int width, int cliff_start, int cliff_end)
{
    auto world = std::make_unique<World>(width, 10);

    // Clear interior to air (rows 1-8).
    for (int y = 1; y < 9; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            world->getData().at(x, y).replaceMaterial(Material::EnumType::Air, 0.0);
        }
    }

    // Create floor with gap (cliff).
    for (int x = 0; x < width; ++x) {
        if (x >= cliff_start && x <= cliff_end) {
            // Gap - air.
            world->getData().at(x, 8).replaceMaterial(Material::EnumType::Air, 0.0);
        }
        else {
            // Floor - wall.
            world->getData().at(x, 8).replaceMaterial(Material::EnumType::Wall, 1.0);
        }
    }

    return world;
}

/**
 * Print a world state to the log for debugging.
 */
inline void printWorld(const World& world, const std::string& label)
{
    spdlog::info("=== {} ===", label);
    const WorldData& data = world.getData();
    for (int y = 0; y < data.height; ++y) {
        std::string row;
        for (int x = 0; x < data.width; ++x) {
            const Cell& cell = data.at(x, y);
            if (cell.material_type == Material::EnumType::Wall) {
                row += "W";
            }
            else if (cell.material_type == Material::EnumType::Wood) {
                row += "D"; // Duck cell.
            }
            else if (cell.material_type == Material::EnumType::Air || cell.isEmpty()) {
                row += ".";
            }
            else {
                row += "?";
            }
        }
        spdlog::info("  {}", row);
    }
}

} // namespace DirtSim::Test
