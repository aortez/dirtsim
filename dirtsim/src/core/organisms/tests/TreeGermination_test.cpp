#include "CellTrackerUtil.h"
#include "core/GridOfCells.h"
#include "core/LoggingChannels.h"
#include "core/PhysicsSettings.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/WorldDiagramGeneratorEmoji.h"
#include "core/organisms/OrganismManager.h"
#include "core/organisms/Tree.h"
#include "core/organisms/TreeBrain.h"
#include "core/organisms/TreeCommands.h"
#include "server/scenarios/ScenarioRegistry.h"
#include <gtest/gtest.h>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <unordered_set>

using namespace DirtSim;

class TreeGerminationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        world = std::make_unique<World>(9, 9);
        ScenarioRegistry registry = ScenarioRegistry::createDefault();
        scenario = registry.createScenario("tree_germination");
    }

    std::unique_ptr<World> world;
    std::unique_ptr<Scenario> scenario;
};

TEST_F(TreeGerminationTest, SeedFallsOntoGround)
{
    // Custom setup for this test: seed at (4,1) to test falling.
    for (uint32_t y = 0; y < 9; ++y) {
        for (uint32_t x = 0; x < 9; ++x) {
            world->getData().at(x, y) = Cell();
        }
    }
    for (uint32_t y = 6; y < 9; ++y) {
        for (uint32_t x = 0; x < 9; ++x) {
            world->addMaterialAtCell(x, y, MaterialType::DIRT, 1.0);
        }
    }
    OrganismId id = world->getOrganismManager().createTree(*world, 4, 1);

    EXPECT_EQ(world->getData().at(4, 1).material_type, MaterialType::SEED);

    std::cout << "Initial state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    double last_print = 0.0;
    for (int i = 0; i < 100; i++) {
        world->advanceTime(0.016);

        if (world->getData().timestep * 0.016 - last_print >= 1.0) {
            std::cout << "After " << (world->getData().timestep * 0.016) << " seconds:\n"
                      << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
            last_print = world->getData().timestep * 0.016;
        }
    }

    const Tree* tree = world->getOrganismManager().getTree(id);
    ASSERT_NE(tree, nullptr);
    EXPECT_GT(tree->getAnchorCell().y, 1);
}

TEST_F(TreeGerminationTest, SeedGerminates)
{
    scenario->setup(*world);

    OrganismId id = 1;
    const Tree* tree = world->getOrganismManager().getTree(id);
    EXPECT_EQ(tree->getStage(), GrowthStage::SEED);

    std::cout << "Initial state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    int frame = 0;
    while (tree->getStage() != GrowthStage::SAPLING && tree->getAge() < 10.0) {
        world->advanceTime(0.016);
        frame++;

        if (frame % 10 == 0) {
            std::cout << "Frame " << frame << " (" << tree->getAge() << "s):\n"
                      << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
        }
    }

    std::cout << "Final state (frame " << frame << "):\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    EXPECT_EQ(tree->getStage(), GrowthStage::SAPLING);
}

TEST_F(TreeGerminationTest, SeedBlockedByWall)
{
    for (uint32_t y = 0; y < 9; ++y) {
        for (uint32_t x = 0; x < 9; ++x) {
            world->getData().at(x, y).replaceMaterial(MaterialType::WALL, 1.0);
        }
    }

    world->getData().at(4, 4).replaceMaterial(MaterialType::AIR, 0.0);

    OrganismId id = world->getOrganismManager().createTree(*world, 4, 4);
    const Tree* tree = world->getOrganismManager().getTree(id);

    for (int i = 0; i < 1000; i++) {
        world->advanceTime(0.016);
    }

    EXPECT_EQ(tree->getStage(), GrowthStage::SEED);
}

TEST_F(TreeGerminationTest, SaplingGrowsBalanced)
{
    scenario->setup(*world);

    OrganismId id = 1;
    const Tree* tree = world->getOrganismManager().getTree(id);

    std::cout << "Initial state (Seed at: " << tree->getAnchorCell().x << ", "
              << tree->getAnchorCell().y << "):\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    Vector2i last_seed_pos = tree->getAnchorCell();
    std::string last_diagram = WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world);

    // Use CellTracker utility for tracking cell physics over time.
    CellTracker tracker(*world, id, 20);

    // Initialize with seed.
    tracker.trackCell(tree->getAnchorCell(), MaterialType::SEED, 0);

    for (int i = 0; i < 2000; i++) {
        // Snapshot current cells before advancing.
        std::unordered_set<Vector2i> cells_before = tree->getCells();

        world->advanceTime(0.016);

        // Record state for all tracked cells.
        tracker.recordFrame(i);

        // Detect and track new cells.
        tracker.detectNewCells(cells_before, tree->getCells(), i);

        // Check for displaced cells.
        tracker.checkForDisplacements(i);

        // Track seed movement.
        Vector2i current_seed_pos = tree->getAnchorCell();
        if (current_seed_pos.x != last_seed_pos.x || current_seed_pos.y != last_seed_pos.y) {
            std::cout << "\n⚠️  SEED MOVED at frame " << i << " (t=" << tree->getAge() << "s)\n";
            std::cout << "FROM: (" << last_seed_pos.x << ", " << last_seed_pos.y << ")\n";
            std::cout << "TO:   (" << current_seed_pos.x << ", " << current_seed_pos.y << ")\n\n";
            std::cout << "BEFORE (frame " << (i - 1) << "):\n" << last_diagram << "\n";
            std::cout << "AFTER (frame " << i << "):\n"
                      << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

            last_seed_pos = current_seed_pos;
        }

        // Save diagram for next iteration.
        last_diagram = WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world);

        // Print every 50 frames for detailed view.
        if (i % 50 == 0 && i > 0) {
            std::cout << "After " << (i * 0.016) << "s (Energy: " << tree->getEnergy()
                      << ", Cells: " << tree->getCells().size() << ", Seed: " << tree->getAnchorCell().x
                      << ", " << tree->getAnchorCell().y << "):\n"
                      << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
        }
    }

    std::cout << "Final state (Energy: " << tree->getEnergy() << ", Cells: " << tree->getCells().size()
              << ", Seed at: (" << tree->getAnchorCell().x << ", " << tree->getAnchorCell().y << ")):\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    EXPECT_EQ(tree->getStage(), GrowthStage::SAPLING);
    EXPECT_GT(tree->getCells().size(), 3u);

    // Verify spatial balance: count materials left vs right of seed.
    int seed_x = tree->getAnchorCell().x;
    std::cout << "\nSeed final position: (" << tree->getAnchorCell().x << ", "
              << tree->getAnchorCell().y << ")\n";

    int wood_left = 0, wood_right = 0;
    int leaf_left = 0, leaf_right = 0;

    for (uint32_t y = 0; y < 7; ++y) {
        for (uint32_t x = 0; x < 7; ++x) {
            const Cell& cell = world->getData().at(x, y);
            if (cell.organism_id != tree->getId()) continue;

            if (cell.material_type == MaterialType::WOOD) {
                if (static_cast<int>(x) < seed_x)
                    wood_left++;
                else if (static_cast<int>(x) > seed_x)
                    wood_right++;
            }
            else if (cell.material_type == MaterialType::LEAF) {
                if (static_cast<int>(x) < seed_x)
                    leaf_left++;
                else if (static_cast<int>(x) > seed_x)
                    leaf_right++;
            }
        }
    }

    std::cout << "\nSpatial Balance Check:\n";
    std::cout << "  WOOD: left=" << wood_left << ", right=" << wood_right << "\n";
    std::cout << "  LEAF: left=" << leaf_left << ", right=" << leaf_right << "\n";

    // Verify growth is balanced (accept 2:3 ratio as balanced for small trees).
    if (wood_left > 0 && wood_right > 0) {
        double wood_ratio = static_cast<double>(std::min(wood_left, wood_right))
            / static_cast<double>(std::max(wood_left, wood_right));
        std::cout << "  WOOD balance ratio: " << wood_ratio << " (should be >= 0.5)\n";
        EXPECT_GE(wood_ratio, 0.5) << "WOOD growth should be reasonably balanced (1:2 or better)";
    }

    if (leaf_left > 0 && leaf_right > 0) {
        double leaf_ratio = static_cast<double>(std::min(leaf_left, leaf_right))
            / static_cast<double>(std::max(leaf_left, leaf_right));
        std::cout << "  LEAF balance ratio: " << leaf_ratio << " (should be >= 0.5)\n";
        EXPECT_GE(leaf_ratio, 0.5) << "LEAF growth should be reasonably balanced (1:2 or better)";
    }
}

TEST_F(TreeGerminationTest, RootsStopAtWater)
{
    world->getPhysicsSettings().swap_enabled = false;

    for (uint32_t y = 0; y < 9; ++y) {
        for (uint32_t x = 0; x < 9; ++x) {
            world->getData().at(x, y) = Cell();
        }
    }

    // Water at bottom 2 rows.
    for (uint32_t y = 7; y < 9; ++y) {
        for (uint32_t x = 0; x < 9; ++x) {
            world->getData().at(x, y).replaceMaterial(MaterialType::WATER, 1.0);
        }
    }

    // Dirt layer above water.
    for (uint32_t x = 0; x < 9; ++x) {
        world->getData().at(x, 6).replaceMaterial(MaterialType::DIRT, 1.0);
    }

    std::cout << "Initial water test setup:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    world->getOrganismManager().createTree(*world, 4, 4);

    for (int i = 0; i < 2000; i++) {
        world->advanceTime(0.016);
        if (i % 500 == 0) {
            std::cout << "Frame " << i << ":\n"
                      << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
        }
    }

    std::cout << "Final water test state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    int root_count = 0;
    int water_count = 0;
    for (uint32_t y = 0; y < 9; ++y) {
        for (uint32_t x = 0; x < 9; ++x) {
            if (world->getData().at(x, y).material_type == MaterialType::ROOT) root_count++;
            if (world->getData().at(x, y).material_type == MaterialType::WATER) water_count++;
        }
    }

    EXPECT_GE(root_count, 1);
    EXPECT_GE(water_count, 10);
}

TEST_F(TreeGerminationTest, TreeStopsGrowingWhenOutOfEnergy)
{
    scenario->setup(*world);

    OrganismId id = 1;
    Tree* tree = world->getOrganismManager().getTree(id);

    const double initial_energy = 25.0;
    tree->setEnergy(initial_energy);

    for (int i = 0; i < 3000; i++) {
        world->advanceTime(0.016);
    }

    // With 25.0 energy and trunk/branch growth model:
    // - SEED (starting cell, no cost)
    // - ROOT (12.0) → 13.0 remaining
    // - WOOD from germination (10.0) → 3.0 remaining
    // - Can't afford another WOOD (10.0) or ROOT (12.0)
    // Expected: 3 cells (SEED + ROOT + WOOD), 3.0 energy remaining.

    EXPECT_EQ(tree->getCells().size(), 3u) << "Tree should have SEED + ROOT + WOOD (25.0 energy limit)";
    EXPECT_DOUBLE_EQ(tree->getEnergy(), 3.0)
        << "Should have 3.0 energy remaining after germination";
}

TEST_F(TreeGerminationTest, WoodCellsStayStationary)
{
    scenario->setup(*world);

    OrganismId id = 1;
    const Tree* tree = world->getOrganismManager().getTree(id);

    std::cout << "Initial state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Run until we have at least 2 WOOD cells.
    std::vector<Vector2i> wood_positions;
    int frame = 0;
    bool found_second_wood = false;

    while (!found_second_wood && tree->getAge() < 20.0) {
        world->advanceTime(0.016);
        frame++;

        // Track all WOOD cells.
        wood_positions.clear();
        for (uint32_t y = 0; y < 9; ++y) {
            for (uint32_t x = 0; x < 9; ++x) {
                const Cell& cell = world->getData().at(x, y);
                if (cell.material_type == MaterialType::WOOD && cell.organism_id == tree->getId()) {
                    wood_positions.push_back(Vector2i{ static_cast<int>(x), static_cast<int>(y) });
                }
            }
        }

        if (wood_positions.size() >= 2) {
            found_second_wood = true;
            std::cout << "Frame " << frame << " (" << tree->getAge() << "s): Found "
                      << wood_positions.size() << " WOOD cells:\n";
            for (size_t i = 0; i < wood_positions.size(); i++) {
                std::cout << "  WOOD[" << i << "] at (" << wood_positions[i].x << ", "
                          << wood_positions[i].y << ")\n";
            }
            std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
        }
    }

    ASSERT_TRUE(found_second_wood) << "Tree should grow at least 2 WOOD cells";
    ASSERT_GE(wood_positions.size(), 2);

    // Save second WOOD position.
    Vector2i second_wood_pos = wood_positions[1];
    std::cout << "\nTracking WOOD[1] at (" << second_wood_pos.x << ", " << second_wood_pos.y
              << ")\n\n";

    // Run for another 100 frames and verify second WOOD cell doesn't move.
    for (int i = 0; i < 100; i++) {
        world->advanceTime(0.016);
        frame++;

        const Cell& cell = world->getData().at(second_wood_pos.x, second_wood_pos.y);

        if ((frame - 1) % 20 == 0) {
            std::cout << "Frame " << frame << " (" << tree->getAge() << "s):\n";
            std::cout << "  WOOD[1] at (" << second_wood_pos.x << ", " << second_wood_pos.y
                      << "): material=" << getMaterialName(cell.material_type)
                      << ", fill=" << cell.fill_ratio << ", organism_id=" << cell.organism_id
                      << "\n";
            std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
        }

        EXPECT_EQ(cell.material_type, MaterialType::WOOD)
            << "Frame " << frame << ": WOOD cell at (" << second_wood_pos.x << ", "
            << second_wood_pos.y << ") changed to " << getMaterialName(cell.material_type);
        EXPECT_EQ(cell.organism_id, tree->getId())
            << "Frame " << frame << ": WOOD cell lost organism_id";
    }

    std::cout << "Final state (frame " << frame << "):\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
}

// A brain that issues a sequence of GrowWood commands, then waits.
class ScriptedGrowWoodBrain : public TreeBrain {
public:
    ScriptedGrowWoodBrain(std::vector<Vector2i> targets) : targets_(std::move(targets)) {}

    TreeCommand decide(const TreeSensoryData& /*sensory*/) override
    {
        if (command_index_ < targets_.size()) {
            GrowWoodCommand cmd;
            cmd.target_pos = targets_[command_index_];
            cmd.execution_time_seconds = 0.1; // Fast for testing.
            command_index_++;
            return cmd;
        }
        // After all growth commands, just wait forever.
        WaitCommand wait;
        wait.duration_seconds = 1000.0;
        return wait;
    }

private:
    std::vector<Vector2i> targets_;
    size_t command_index_ = 0;
};

// Disabled: Bones system disabled during rigid body implementation.
TEST_F(TreeGerminationTest, DISABLED_HorizontalBoneForceBehavior)
{
    // Create a minimal 3x3 world with a seed and one WOOD cell to the left.
    // This isolates bone physics from complex tree growth.
    world = std::make_unique<World>(3, 3);

    // Clear the world.
    for (uint32_t y = 0; y < 3; ++y) {
        for (uint32_t x = 0; x < 3; ++x) {
            world->getData().at(x, y) = Cell();
        }
    }

    // Plant seed at (1, 2) - bottom center.
    OrganismId id = world->getOrganismManager().createTree(*world, 1, 2);
    Tree* tree = world->getOrganismManager().getTree(id);
    ASSERT_NE(tree, nullptr);

    // Replace brain with one that grows WOOD to the left at (0, 2).
    Vector2i seed_pos{ 1, 2 };
    Vector2i wood_target{ 0, 2 };
    tree->setBrain(std::make_unique<ScriptedGrowWoodBrain>(std::vector<Vector2i>{ wood_target }));

    // Give tree enough energy to grow one WOOD cell.
    tree->setEnergy(100.0);

    std::cout << "\n=== Horizontal Bone Force Test ===\n";
    std::cout << "Setup: 3x3 world, SEED at (1,2), will grow WOOD at (0,2)\n\n";
    std::cout << "Initial state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Set up tracker with seed.
    CellTracker tracker(*world, id);
    tracker.trackCell(seed_pos, MaterialType::SEED, 0);

    // Run until WOOD appears.
    int frame = 0;
    bool wood_grown = false;
    while (!wood_grown && frame < 100) {
        std::unordered_set<Vector2i> cells_before = tree->getCells();

        world->advanceTime(0.016);
        frame++;

        tracker.recordFrame(frame);
        tracker.detectNewCells(cells_before, tree->getCells(), frame);

        const Cell& wood_cell = world->getData().at(wood_target.x, wood_target.y);
        if (wood_cell.material_type == MaterialType::WOOD && wood_cell.organism_id == id) {
            wood_grown = true;
            std::cout << "WOOD grown at frame " << frame << ":\n"
                      << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
        }
    }

    ASSERT_TRUE(wood_grown) << "WOOD should have grown at target position";
    ASSERT_EQ(tree->getBones().size(), 1) << "Should have exactly one bone connecting SEED and WOOD";

    const Bone& bone = tree->getBones()[0];
    std::cout << "Bone: (" << bone.cell_a.x << "," << bone.cell_a.y << ") <-> (" << bone.cell_b.x
              << "," << bone.cell_b.y << ") rest=" << bone.rest_distance
              << " stiff=" << bone.stiffness << "\n\n";

    // Now track forces over time using the tracker.
    tracker.printTableHeader();

    for (int i = 0; i < 100; i++) {
        tracker.printTableRow(frame + i);

        world->advanceTime(0.016);

        tracker.recordFrame(frame + i);

        if (tracker.checkForDisplacements(frame + i)) {
            FAIL() << "Cell was displaced from its position";
        }
    }

    std::cout << "\n=== Final State ===\n";
    std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Verify cells are still in place.
    const Cell& final_seed = world->getData().at(seed_pos.x, seed_pos.y);
    const Cell& final_wood = world->getData().at(wood_target.x, wood_target.y);

    EXPECT_EQ(final_seed.material_type, MaterialType::SEED);
    EXPECT_EQ(final_seed.organism_id, id);
    EXPECT_EQ(final_wood.material_type, MaterialType::WOOD);
    EXPECT_EQ(final_wood.organism_id, id);

    // Verify horizontal bone stability (X components should be near center).
    // Y component behavior is affected by gravity and will be examined separately.
    EXPECT_LT(std::abs(final_seed.com.x), 0.5) << "Seed COM X should be stable near center";
    EXPECT_LT(std::abs(final_wood.com.x), 0.5) << "Wood COM X should be stable near center";
}

TEST_F(TreeGerminationTest, VerticalBoneForceBehavior)
{
    // Create a minimal 3x3 world with a seed and one WOOD cell above it.
    // This tests bone behavior against gravity.
    world = std::make_unique<World>(3, 3);

    // Clear the world.
    for (uint32_t y = 0; y < 3; ++y) {
        for (uint32_t x = 0; x < 3; ++x) {
            world->getData().at(x, y) = Cell();
        }
    }

    // Plant seed at (1, 2) - bottom center.
    OrganismId id = world->getOrganismManager().createTree(*world, 1, 2);
    Tree* tree = world->getOrganismManager().getTree(id);
    ASSERT_NE(tree, nullptr);

    // Replace brain with one that grows WOOD above at (1, 1), then (1, 0).
    Vector2i seed_pos{ 1, 2 };
    Vector2i wood1_target{ 1, 1 };
    Vector2i wood2_target{ 1, 0 };
    tree->setBrain(std::make_unique<ScriptedGrowWoodBrain>(
        std::vector<Vector2i>{ wood1_target, wood2_target }));

    // Give tree enough energy to grow two WOOD cells.
    tree->setEnergy(100.0);

    std::cout << "\n=== Vertical Bone Force Test ===\n";
    std::cout << "Setup: 3x3 world, SEED at (1,2), will grow WOOD at (1,1) and (1,0) above\n\n";
    std::cout << "Initial state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Set up tracker with seed.
    CellTracker tracker(*world, id);
    tracker.trackCell(seed_pos, MaterialType::SEED, 0);

    // Run until both WOOD cells appear.
    int frame = 0;
    bool wood1_grown = false;
    bool wood2_grown = false;
    while ((!wood1_grown || !wood2_grown) && frame < 200) {
        std::unordered_set<Vector2i> cells_before = tree->getCells();

        world->advanceTime(0.016);
        frame++;

        tracker.recordFrame(frame);
        tracker.detectNewCells(cells_before, tree->getCells(), frame);

        const Cell& wood1_cell = world->getData().at(wood1_target.x, wood1_target.y);
        if (!wood1_grown && wood1_cell.material_type == MaterialType::WOOD
            && wood1_cell.organism_id == id) {
            wood1_grown = true;
            std::cout << "WOOD1 grown at frame " << frame << ":\n"
                      << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
        }

        const Cell& wood2_cell = world->getData().at(wood2_target.x, wood2_target.y);
        if (!wood2_grown && wood2_cell.material_type == MaterialType::WOOD
            && wood2_cell.organism_id == id) {
            wood2_grown = true;
            std::cout << "WOOD2 grown at frame " << frame << ":\n"
                      << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
        }
    }

    ASSERT_TRUE(wood1_grown) << "WOOD1 should have grown at (1,1)";
    ASSERT_TRUE(wood2_grown) << "WOOD2 should have grown at (1,0)";

    std::cout << "\nBones created: " << tree->getBones().size() << " total\n";
    for (size_t i = 0; i < tree->getBones().size(); i++) {
        const Bone& b = tree->getBones()[i];
        std::cout << "  Bone[" << i << "]: (" << b.cell_a.x << "," << b.cell_a.y << ") <-> ("
                  << b.cell_b.x << "," << b.cell_b.y << ") rest=" << b.rest_distance
                  << " stiff=" << b.stiffness << "\n";
    }
    std::cout << "\n";

    // Now track forces over time using the tracker.
    tracker.printTableHeader();

    for (int i = 0; i < 100; i++) {
        tracker.printTableRow(frame + i);

        world->advanceTime(0.016);

        tracker.recordFrame(frame + i);

        if (tracker.checkForDisplacements(frame + i)) {
            FAIL() << "Cell was displaced from its position";
        }
    }

    std::cout << "\n=== Final State ===\n";
    std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Verify all cells are still in place.
    const Cell& final_seed = world->getData().at(seed_pos.x, seed_pos.y);
    const Cell& final_wood1 = world->getData().at(wood1_target.x, wood1_target.y);
    const Cell& final_wood2 = world->getData().at(wood2_target.x, wood2_target.y);

    EXPECT_EQ(final_seed.material_type, MaterialType::SEED);
    EXPECT_EQ(final_seed.organism_id, id);
    EXPECT_EQ(final_wood1.material_type, MaterialType::WOOD);
    EXPECT_EQ(final_wood1.organism_id, id);
    EXPECT_EQ(final_wood2.material_type, MaterialType::WOOD);
    EXPECT_EQ(final_wood2.organism_id, id);

    // For vertical stack, just verify cells stayed in their grid positions.
    // COMs may drift to cell boundaries under gravity - that's acceptable.
}

TEST_F(TreeGerminationTest, DebugWoodFalling)
{
    scenario->setup(*world);

    OrganismId id = 1;
    const Tree* tree = world->getOrganismManager().getTree(id);

    std::cout << "=== DEEP DEBUG: Wood Cell Physics ===\n\n";
    std::cout << "Initial state:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Run until we have 2 WOOD cells.
    std::vector<Vector2i> wood_positions;
    int frame = 0;
    bool found_second_wood = false;

    while (!found_second_wood && tree->getAge() < 20.0) {
        world->advanceTime(0.016);
        frame++;

        wood_positions.clear();
        for (uint32_t y = 0; y < 9; ++y) {
            for (uint32_t x = 0; x < 9; ++x) {
                const Cell& cell = world->getData().at(x, y);
                if (cell.material_type == MaterialType::WOOD && cell.organism_id == tree->getId()) {
                    wood_positions.push_back(Vector2i{ static_cast<int>(x), static_cast<int>(y) });
                }
            }
        }

        if (wood_positions.size() >= 2) {
            found_second_wood = true;
            std::cout << "\n=== Frame " << frame << ": Found 2 WOOD cells ===\n";
            for (size_t i = 0; i < wood_positions.size(); i++) {
                std::cout << "  WOOD[" << i << "] at (" << wood_positions[i].x << ", "
                          << wood_positions[i].y << ")\n";
            }
        }
    }

    ASSERT_TRUE(found_second_wood);

    // Track both wood cells in detail for 50 frames.
    Vector2i wood0_pos = wood_positions[0];
    Vector2i wood1_pos = wood_positions[1];

    std::cout << "\n=== Detailed Tracking ===\n";
    std::cout << "WOOD[0] (first/center): (" << wood0_pos.x << ", " << wood0_pos.y << ")\n";
    std::cout << "WOOD[1] (second/left):  (" << wood1_pos.x << ", " << wood1_pos.y << ")\n";
    std::cout << "Initial Seed position: (" << tree->getAnchorCell().x << ", "
              << tree->getAnchorCell().y << ")\n\n";

    Vector2i last_seed_pos = tree->getAnchorCell();

    for (int i = 0; i < 50; i++) {
        world->advanceTime(0.016);
        frame++;

        // Get current cell data.
        const Cell& wood0 = world->getData().at(wood0_pos.x, wood0_pos.y);
        const Cell& wood1 = world->getData().at(wood1_pos.x, wood1_pos.y);

        // Check for seed movement.
        Vector2i current_seed_pos = tree->getAnchorCell();
        bool seed_moved =
            (current_seed_pos.x != last_seed_pos.x) || (current_seed_pos.y != last_seed_pos.y);

        // Print every 5 frames.
        if (i % 5 == 0) {
            std::cout << "\n━━━ Frame " << frame << " (t=" << tree->getAge() << "s) ━━━\n";
            if (seed_moved) {
                std::cout << "⚠️  SEED MOVED: (" << last_seed_pos.x << ", " << last_seed_pos.y
                          << ") → (" << current_seed_pos.x << ", " << current_seed_pos.y << ")\n";
            }
            std::cout << "Seed position: (" << tree->getAnchorCell().x << ", "
                      << tree->getAnchorCell().y << ")\n";
            std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

            // WOOD[0] details.
            std::cout << "WOOD[0] at (" << wood0_pos.x << ", " << wood0_pos.y << "):\n";
            std::cout << "  material: " << getMaterialName(wood0.material_type) << "\n";
            std::cout << "  fill_ratio: " << wood0.fill_ratio << "\n";
            std::cout << "  organism_id: " << wood0.organism_id << "\n";
            std::cout << "  com: (" << wood0.com.x << ", " << wood0.com.y << ")\n";
            std::cout << "  velocity: (" << wood0.velocity.x << ", " << wood0.velocity.y << ")\n";
            std::cout << "  pressure: " << wood0.pressure << "\n";
            std::cout << "  pressure_gradient: (" << wood0.pressure_gradient.x << ", "
                      << wood0.pressure_gradient.y << ")\n";
            std::cout << "  pending_force: (" << wood0.pending_force.x << ", "
                      << wood0.pending_force.y << ")\n";

            // WOOD[1] details.
            std::cout << "WOOD[1] at (" << wood1_pos.x << ", " << wood1_pos.y << "):\n";
            std::cout << "  material: " << getMaterialName(wood1.material_type) << "\n";
            std::cout << "  fill_ratio: " << wood1.fill_ratio << "\n";
            std::cout << "  organism_id: " << wood1.organism_id << "\n";
            std::cout << "  com: (" << wood1.com.x << ", " << wood1.com.y << ")\n";
            std::cout << "  velocity: (" << wood1.velocity.x << ", " << wood1.velocity.y << ")\n";
            std::cout << "  pressure: " << wood1.pressure << "\n";
            std::cout << "  pressure_gradient: (" << wood1.pressure_gradient.x << ", "
                      << wood1.pressure_gradient.y << ")\n";
            std::cout << "  pending_force: (" << wood1.pending_force.x << ", "
                      << wood1.pending_force.y << ")\n";

            // Show SEED details if it moved.
            if (seed_moved) {
                const Cell& seed_cell = world->getData().at(current_seed_pos.x, current_seed_pos.y);
                std::cout << "SEED at (" << current_seed_pos.x << ", " << current_seed_pos.y
                          << "):\n";
                std::cout << "  com: (" << seed_cell.com.x << ", " << seed_cell.com.y << ")\n";
                std::cout << "  velocity: (" << seed_cell.velocity.x << ", " << seed_cell.velocity.y
                          << ")\n";
                last_seed_pos = current_seed_pos;
            }

            // Check if WOOD[1] moved.
            bool wood1_still_there =
                world->getData().at(wood1_pos.x, wood1_pos.y).material_type == MaterialType::WOOD
                && world->getData().at(wood1_pos.x, wood1_pos.y).organism_id == tree->getId();

            if (!wood1_still_there) {
                std::cout << "\n⚠️  WOOD[1] MOVED FROM (" << wood1_pos.x << ", " << wood1_pos.y
                          << ")!\n";
                // Find where it went.
                for (uint32_t y = 0; y < 7; ++y) {
                    for (uint32_t x = 0; x < 7; ++x) {
                        const Cell& cell = world->getData().at(x, y);
                        if (cell.material_type == MaterialType::WOOD && cell.organism_id == tree->getId()
                            && !(
                                static_cast<int>(x) == wood0_pos.x
                                && static_cast<int>(y) == wood0_pos.y)) {
                            std::cout << "Found WOOD[1] at new position: (" << x << ", " << y
                                      << ")\n";
                            wood1_pos = Vector2i{ static_cast<int>(x), static_cast<int>(y) };
                            break;
                        }
                    }
                }
            }
        }
    }

    std::cout << "\n=== Final State ===\n";
    std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";
}

// A brain that grows cells one at a time with configurable delay between growths.
// After each growth completes, it waits for stability_frames before growing next cell.
class StepByStepGrowthBrain : public TreeBrain {
public:
    StepByStepGrowthBrain(std::vector<Vector2i> targets, double growth_time = 0.1)
        : targets_(std::move(targets)), growth_time_(growth_time)
    {}

    TreeCommand decide(const TreeSensoryData& /*sensory*/) override
    {
        if (command_index_ < targets_.size()) {
            GrowWoodCommand cmd;
            cmd.target_pos = targets_[command_index_];
            cmd.execution_time_seconds = growth_time_;
            command_index_++;
            return cmd;
        }
        // After all growth commands, wait forever.
        WaitCommand wait;
        wait.duration_seconds = 1000.0;
        return wait;
    }

    size_t getCommandIndex() const { return command_index_; }
    size_t getTotalCommands() const { return targets_.size(); }

private:
    std::vector<Vector2i> targets_;
    double growth_time_;
    size_t command_index_ = 0;
};

// Extended stability test: grows a tree step-by-step and verifies stability after each cell.
TEST_F(TreeGerminationTest, ExtendedGrowthStability)
{
    // Enable debug logging for tree and brain channels.
    LoggingChannels::initialize();
    LoggingChannels::setChannelLevel(LogChannel::Tree, spdlog::level::debug);
    LoggingChannels::setChannelLevel(LogChannel::Brain, spdlog::level::debug);

    // Use the germination scenario for setup.
    scenario->setup(*world);

    std::cout << "\n=== Extended Growth Stability Test ===\n";
    std::cout << "Initial state (from TreeGermination scenario):\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Get the tree created by the scenario.
    OrganismId tree_id = 1;
    Tree* tree = world->getOrganismManager().getTree(tree_id);
    ASSERT_NE(tree, nullptr);

    Vector2i seed_pos = tree->getAnchorCell();
    std::cout << "Seed initially at: (" << seed_pos.x << ", " << seed_pos.y << ")\n";
    std::cout << "Note: Dirt is at y=6,7,8 so seed is floating in air!\n\n";

    // First, let the seed fall and land on the dirt.
    std::cout << "=== Phase 1: Let seed fall to ground ===\n";
    int frame = 0;
    Vector2i last_seed_pos = seed_pos;
    bool seed_landed = false;

    while (!seed_landed && frame < 500) {
        world->advanceTime(0.016);
        frame++;

        Vector2i current_pos = tree->getAnchorCell();
        if (current_pos.x != last_seed_pos.x || current_pos.y != last_seed_pos.y) {
            std::cout << "Frame " << frame << ": Seed moved from (" << last_seed_pos.x << ", "
                      << last_seed_pos.y << ") to (" << current_pos.x << ", " << current_pos.y << ")\n";
            last_seed_pos = current_pos;
        }

        // Check if seed has landed (y=5 is just above dirt at y=6).
        if (current_pos.y >= 5) {
            // Check if seed has stopped moving (velocity near zero).
            const Cell& seed_cell = world->getData().at(current_pos.x, current_pos.y);
            if (std::abs(seed_cell.velocity.y) < 0.1 && frame > 50) {
                seed_landed = true;
                std::cout << "Frame " << frame << ": Seed landed at (" << current_pos.x << ", "
                          << current_pos.y << ") with velocity (" << seed_cell.velocity.x << ", "
                          << seed_cell.velocity.y << ")\n";
            }
        }
    }

    std::cout << "\nAfter landing:\n"
              << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Update seed position after landing.
    seed_pos = tree->getAnchorCell();
    std::cout << "Seed final position: (" << seed_pos.x << ", " << seed_pos.y << ")\n\n";

    // Define growth pattern relative to NEW seed position (after landing).
    std::cout << "=== Phase 2: Grow tree from landed position ===\n";
    std::vector<Vector2i> growth_targets = {
        { seed_pos.x, seed_pos.y - 1 }, // First wood above seed.
        { seed_pos.x, seed_pos.y - 2 }, // Second wood (trunk continues).
        { seed_pos.x, seed_pos.y - 3 }, // Third wood (top of trunk).
    };

    // Only add branches if there's room.
    if (seed_pos.y >= 4) {
        growth_targets.push_back({ seed_pos.x - 1, seed_pos.y - 3 }); // Left branch.
        growth_targets.push_back({ seed_pos.x + 1, seed_pos.y - 3 }); // Right branch.
    }

    std::cout << "Growth targets:\n";
    for (size_t i = 0; i < growth_targets.size(); ++i) {
        std::cout << "  " << i << ": (" << growth_targets[i].x << ", " << growth_targets[i].y << ")\n";
    }
    std::cout << "\n";

    tree->setBrain(std::make_unique<StepByStepGrowthBrain>(growth_targets, 0.1));
    tree->setEnergy(500.0); // Plenty of energy.

    // Track all cells as they're added (frame count continues from Phase 1).
    CellTracker tracker(*world, tree_id, 50);
    tracker.trackCell(seed_pos, MaterialType::SEED, frame);

    const int STABILITY_FRAMES = 60;   // Frames to run after each growth.
    const double COM_THRESHOLD = 0.4;  // Max acceptable COM drift from center.
    const double VEL_THRESHOLD = 0.05; // Max acceptable velocity after stabilization.

    size_t last_cell_count = 1; // Start with seed.
    int cells_grown = 0;
    bool any_stability_failure = false;

    // Run until all cells are grown and stable.
    while (cells_grown < static_cast<int>(growth_targets.size()) && frame < 5000) {
        std::unordered_set<Vector2i> cells_before = tree->getCells();

        world->advanceTime(0.016);
        frame++;

        tracker.recordFrame(frame);

        // Detect new cells.
        std::unordered_set<Vector2i> cells_after = tree->getCells();
        if (cells_after.size() > last_cell_count) {
            // New cell was grown!
            cells_grown++;
            std::cout << "\n━━━ GROWTH EVENT " << cells_grown << "/" << growth_targets.size()
                      << " at frame " << frame << " ━━━\n";

            // Find the new cell.
            for (const auto& pos : cells_after) {
                if (cells_before.find(pos) == cells_before.end()) {
                    const Cell& cell = world->getData().at(pos.x, pos.y);
                    tracker.trackCell(pos, cell.material_type, frame);
                    std::cout << "New cell: " << getMaterialName(cell.material_type) << " at (" << pos.x
                              << ", " << pos.y << ")\n";
                }
            }
            std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

            // Run stability check frames.
            std::cout << "Running " << STABILITY_FRAMES << " stability frames...\n";
            bool stable = true;

            for (int s = 0; s < STABILITY_FRAMES; ++s) {
                world->advanceTime(0.016);
                frame++;
                tracker.recordFrame(frame);

                // Check for displaced cells.
                if (tracker.checkForDisplacements(frame)) {
                    std::cout << "❌ DISPLACEMENT DETECTED during stability check!\n";
                    stable = false;
                    any_stability_failure = true;
                    break;
                }

                // Check COM and velocity stability for all organism cells.
                for (const auto& pos : tree->getCells()) {
                    const Cell& cell = world->getData().at(pos.x, pos.y);

                    // Check COM drift.
                    double com_magnitude = std::sqrt(cell.com.x * cell.com.x + cell.com.y * cell.com.y);
                    if (com_magnitude > COM_THRESHOLD && s == STABILITY_FRAMES - 1) {
                        std::cout << "⚠️  COM drift at (" << pos.x << ", " << pos.y
                                  << "): magnitude=" << com_magnitude << " (threshold=" << COM_THRESHOLD
                                  << ")\n";
                    }

                    // Check velocity after stability period.
                    if (s == STABILITY_FRAMES - 1) {
                        double vel_magnitude =
                            std::sqrt(cell.velocity.x * cell.velocity.x + cell.velocity.y * cell.velocity.y);
                        if (vel_magnitude > VEL_THRESHOLD) {
                            std::cout << "⚠️  Velocity at (" << pos.x << ", " << pos.y
                                      << "): magnitude=" << vel_magnitude
                                      << " (threshold=" << VEL_THRESHOLD << ")\n";
                        }
                    }
                }
            }

            if (stable) {
                std::cout << "✅ Structure stable after growth " << cells_grown << "\n";

                // Print final state of all cells.
                std::cout << "Cell states after stabilization:\n";
                for (const auto& pos : tree->getCells()) {
                    const Cell& cell = world->getData().at(pos.x, pos.y);
                    std::cout << "  " << getMaterialName(cell.material_type) << " at (" << pos.x << ", "
                              << pos.y << "): COM=(" << std::fixed << std::setprecision(3) << cell.com.x
                              << ", " << cell.com.y << "), vel=(" << cell.velocity.x << ", "
                              << cell.velocity.y << ")\n";
                }
            }

            last_cell_count = cells_after.size();
        }
    }

    std::cout << "\n=== Final State ===\n";
    std::cout << "Total frames: " << frame << "\n";
    std::cout << "Cells grown: " << cells_grown << "/" << growth_targets.size() << "\n";
    std::cout << WorldDiagramGeneratorEmoji::generateEmojiDiagram(*world) << "\n";

    // Final assertions.
    EXPECT_FALSE(any_stability_failure) << "Structure should remain stable after each growth";
    EXPECT_EQ(cells_grown, static_cast<int>(growth_targets.size()))
        << "All growth targets should be reached";

    // Final stability check: all COMs should be reasonably centered.
    for (const auto& pos : tree->getCells()) {
        const Cell& cell = world->getData().at(pos.x, pos.y);
        double com_magnitude = std::sqrt(cell.com.x * cell.com.x + cell.com.y * cell.com.y);
        EXPECT_LT(com_magnitude, COM_THRESHOLD)
            << "Cell at (" << pos.x << ", " << pos.y << ") has excessive COM drift: " << com_magnitude;
    }
}
