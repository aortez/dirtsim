#include "core/GridOfCells.h"
#include "core/World.h"
#include "core/WorldData.h"

#include <chrono>
#include <gtest/gtest.h>
#include <random>
#include <spdlog/spdlog.h>

using namespace DirtSim;

/**
 * Integration test: Verify that GridOfCells cache produces identical results
 * to direct cell access across a full simulation run.
 */
TEST(GridOfCellsIntegrationTest, CacheProducesIdenticalResults)
{
    // Helper to run deterministic simulation.
    auto runSimulation = [](bool use_cache) -> nlohmann::json {
        // Set cache mode.
        GridOfCells::USE_CACHE = use_cache;

        // Create deterministic 10×10 world.
        World world(10, 10);
        world.setRandomSeed(42); // Deterministic RNG.

        // Populate with deterministic random materials (fixed seed).
        std::mt19937 rng(42);
        std::uniform_int_distribution<> coord_dist(1, 8); // Avoid walls.
        std::uniform_int_distribution<> mat_dist(1, 5);   // Material types.
        std::uniform_real_distribution<> fill_dist(0.3, 1.0);

        for (int i = 0; i < 15; ++i) {
            int16_t x = static_cast<int16_t>(coord_dist(rng));
            int16_t y = static_cast<int16_t>(coord_dist(rng));
            Material::EnumType mat = static_cast<Material::EnumType>(mat_dist(rng));
            float fill = static_cast<float>(fill_dist(rng));
            world.addMaterialAtCell({ x, y }, mat, fill);
        }

        // Run simulation for 100 frames.
        for (int frame = 0; frame < 100; ++frame) {
            world.advanceTime(0.016); // 60 FPS.
        }

        // Return serialized state.
        return world.toJSON();
    };

    auto removeHasSupport = [](nlohmann::json& state) {
        if (state.contains("cells") && state["cells"].is_array()) {
            for (auto& cell : state["cells"]) {
                if (cell.contains("has_support")) {
                    cell.erase("has_support");
                }
            }
        }
    };

    spdlog::info("Case 1: Running without cache (baseline)...");
    nlohmann::json case1_no_cache = runSimulation(false);
    removeHasSupport(case1_no_cache);
    size_t hash1 = std::hash<std::string>{}(case1_no_cache.dump());

    spdlog::info("Case 2: Running with cache...");
    nlohmann::json case2_with_cache = runSimulation(true);
    removeHasSupport(case2_with_cache);
    size_t hash2 = std::hash<std::string>{}(case2_with_cache.dump());

    spdlog::info("Case 3: Running without cache again (control)...");
    nlohmann::json case3_no_cache = runSimulation(false);
    removeHasSupport(case3_no_cache);
    size_t hash3 = std::hash<std::string>{}(case3_no_cache.dump());

    // Log all hashes.
    spdlog::info("Hash 1 (no cache):   {}", hash1);
    spdlog::info("Hash 2 (with cache): {}", hash2);
    spdlog::info("Hash 3 (no cache):   {}", hash3);

    // Verify determinism: Cases 1 and 3 should match.
    EXPECT_EQ(case1_no_cache, case3_no_cache)
        << "Control test failed: Simulation is non-deterministic!\n"
        << "Cases 1 and 3 (both without cache) produced different results.";

    // Verify cache correctness: Case 2 should match Case 1.
    EXPECT_EQ(case1_no_cache, case2_with_cache)
        << "Cache test failed: Cached path differs from direct path!\n"
        << "This indicates a bug in GridOfCells bitmap implementation.";

    // Restore default.
    GridOfCells::USE_CACHE = true;
}

/**
 * Simple single-frame test to isolate divergence.
 */
TEST(GridOfCellsIntegrationTest, SingleFrameComparison)
{
    auto runSingleFrame = [](bool use_cache) -> nlohmann::json {
        GridOfCells::USE_CACHE = use_cache;

        World world(10, 10);
        world.setRandomSeed(42); // Deterministic RNG.

        // Add one dirt cell.
        world.addMaterialAtCell({ 5, 5 }, Material::EnumType::Dirt, 1.0);

        // Run one frame.
        world.advanceTime(0.016);

        return world.toJSON();
    };

    nlohmann::json cached = runSingleFrame(true);
    nlohmann::json direct = runSingleFrame(false);

    if (cached != direct) {
        spdlog::error("DIVERGENCE on first frame!");
        spdlog::error("Cached cell(5,5): {}", cached["cells"][55].dump());
        spdlog::error("Direct cell(5,5): {}", direct["cells"][55].dump());
    }

    EXPECT_EQ(cached, direct) << "Results differ after single frame!";

    GridOfCells::USE_CACHE = true;
}

/**
 * Unit test: Verify GridOfCells bitmap accurately reflects cell emptiness.
 */
TEST(GridOfCellsTest, EmptyCellBitmapMatchesCellState)
{
    World world(20, 20);

    // Add some materials at known locations.
    world.addMaterialAtCell({ 5, 5 }, Material::EnumType::Dirt, 1.0);
    world.addBulkWaterAtCell({ 10, 10 }, 0.5f);
    world.addMaterialAtCell({ 15, 15 }, Material::EnumType::Metal, 0.8);

    // Build grid cache.
    GridOfCells grid(
        world.getData().cells,
        world.getData().debug_info,
        world.getData().width,
        world.getData().height);

    // Verify every cell's bitmap state matches actual cell state.
    int mismatches = 0;
    for (uint32_t y = 0; y < 20; ++y) {
        for (uint32_t x = 0; x < 20; ++x) {
            bool bitmap_says_empty = grid.emptyCells().isSet(x, y);
            bool cell_is_empty = world.getData().at(x, y).isEmpty();

            if (bitmap_says_empty != cell_is_empty) {
                ++mismatches;
                EXPECT_EQ(bitmap_says_empty, cell_is_empty)
                    << "Mismatch at (" << x << "," << y << "): " << "bitmap=" << bitmap_says_empty
                    << " cell=" << cell_is_empty;
            }
        }
    }

    EXPECT_EQ(mismatches, 0) << "Found " << mismatches << " bitmap/cell mismatches";
}

/**
 * Performance comparison test: Measure overhead of cache construction.
 */
TEST(GridOfCellsTest, CacheConstructionOverhead)
{
    // Create a larger world for meaningful timing.
    World world(100, 100);

    // Populate with some materials.
    std::mt19937 rng(123);
    std::uniform_int_distribution<> coord_dist(0, 99);
    std::uniform_int_distribution<> mat_dist(1, 5);

    for (int i = 0; i < 500; ++i) {
        int16_t x = static_cast<int16_t>(coord_dist(rng));
        int16_t y = static_cast<int16_t>(coord_dist(rng));
        Material::EnumType mat = static_cast<Material::EnumType>(mat_dist(rng));
        world.addMaterialAtCell({ x, y }, mat, 0.5f);
    }

    // Measure cache construction time.
    auto start = std::chrono::high_resolution_clock::now();
    GridOfCells grid(
        world.getData().cells,
        world.getData().debug_info,
        world.getData().width,
        world.getData().height);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    spdlog::info("GridOfCells construction (100x100): {} μs", duration.count());

    EXPECT_LT(duration.count(), 5000) << "Cache construction too slow!";
}

/**
 * Verify single-pass populateAll() produces identical caches to three-pass approach.
 */
TEST(GridOfCellsTest, SinglePassMatchesSeparatePasses)
{
    World world(30, 30);

    // Populate with varied materials including edges and corners.
    world.addMaterialAtCell({ 1, 1 }, Material::EnumType::Dirt, 1.0);
    world.addBulkWaterAtCell({ 5, 5 }, 0.7f);
    world.addMaterialAtCell({ 10, 3 }, Material::EnumType::Metal, 0.9f);
    world.addMaterialAtCell({ 15, 15 }, Material::EnumType::Sand, 0.5f);
    world.addMaterialAtCell({ 28, 28 }, Material::EnumType::Dirt, 0.8f);
    world.addBulkWaterAtCell({ 0, 15 }, 0.6f);
    world.addMaterialAtCell({ 29, 0 }, Material::EnumType::Metal, 1.0);

    // Scatter more materials with RNG.
    std::mt19937 rng(99);
    std::uniform_int_distribution<> coord_dist(0, 29);
    std::uniform_real_distribution<> fill_dist(0.2, 1.0);
    const std::array<Material::EnumType, 8> materials{ {
        Material::EnumType::Dirt,
        Material::EnumType::Leaf,
        Material::EnumType::Metal,
        Material::EnumType::Root,
        Material::EnumType::Sand,
        Material::EnumType::Seed,
        Material::EnumType::Wall,
        Material::EnumType::Wood,
    } };
    std::uniform_int_distribution<size_t> materialIndexDist(0, materials.size() - 1);

    for (int i = 0; i < 50; ++i) {
        int16_t x = static_cast<int16_t>(coord_dist(rng));
        int16_t y = static_cast<int16_t>(coord_dist(rng));
        const Material::EnumType mat = materials[materialIndexDist(rng)];
        float fill = static_cast<float>(fill_dist(rng));
        world.addMaterialAtCell({ x, y }, mat, fill);
    }

    const int w = world.getData().width;
    const int h = world.getData().height;

    // Single-pass (constructor uses populateAll).
    GridOfCells single_pass(world.getData().cells, world.getData().debug_info, w, h);

    // Three-pass (rebuild using separate passes).
    GridOfCells three_pass(world.getData().cells, world.getData().debug_info, w, h);
    three_pass.rebuildSeparatePasses();

    // Compare all four data structures.
    int empty_bitmap_mismatches = 0;
    int wall_bitmap_mismatches = 0;
    int empty_neighborhood_mismatches = 0;
    int material_neighborhood_mismatches = 0;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (single_pass.emptyCells().isSet(x, y) != three_pass.emptyCells().isSet(x, y)) {
                ++empty_bitmap_mismatches;
                if (empty_bitmap_mismatches <= 3) {
                    EXPECT_EQ(
                        single_pass.emptyCells().isSet(x, y), three_pass.emptyCells().isSet(x, y))
                        << "empty_cells_ mismatch at (" << x << "," << y << ")";
                }
            }

            if (single_pass.wallCells().isSet(x, y) != three_pass.wallCells().isSet(x, y)) {
                ++wall_bitmap_mismatches;
                if (wall_bitmap_mismatches <= 3) {
                    EXPECT_EQ(
                        single_pass.wallCells().isSet(x, y), three_pass.wallCells().isSet(x, y))
                        << "wall_cells_ mismatch at (" << x << "," << y << ")";
                }
            }

            auto sp_en = single_pass.getEmptyNeighborhood(x, y);
            auto tp_en = three_pass.getEmptyNeighborhood(x, y);
            if (sp_en.raw().data != tp_en.raw().data) {
                ++empty_neighborhood_mismatches;
                if (empty_neighborhood_mismatches <= 3) {
                    EXPECT_EQ(sp_en.raw().data, tp_en.raw().data)
                        << "empty_neighborhoods_ mismatch at (" << x << "," << y << ")"
                        << " single_pass=0x" << std::hex << sp_en.raw().data << " three_pass=0x"
                        << tp_en.raw().data << std::dec;
                }
            }

            auto sp_mn = single_pass.getMaterialNeighborhood(x, y);
            auto tp_mn = three_pass.getMaterialNeighborhood(x, y);
            if (sp_mn.raw() != tp_mn.raw()) {
                ++material_neighborhood_mismatches;
                if (material_neighborhood_mismatches <= 3) {
                    EXPECT_EQ(sp_mn.raw(), tp_mn.raw())
                        << "material_neighborhoods_ mismatch at (" << x << "," << y << ")"
                        << " single_pass=0x" << std::hex << sp_mn.raw() << " three_pass=0x"
                        << tp_mn.raw() << std::dec;
                }
            }
        }
    }

    EXPECT_EQ(empty_bitmap_mismatches, 0)
        << "Total empty bitmap mismatches: " << empty_bitmap_mismatches;
    EXPECT_EQ(wall_bitmap_mismatches, 0)
        << "Total wall bitmap mismatches: " << wall_bitmap_mismatches;
    EXPECT_EQ(empty_neighborhood_mismatches, 0)
        << "Total empty neighborhood mismatches: " << empty_neighborhood_mismatches;
    EXPECT_EQ(material_neighborhood_mismatches, 0)
        << "Total material neighborhood mismatches: " << material_neighborhood_mismatches;
}
