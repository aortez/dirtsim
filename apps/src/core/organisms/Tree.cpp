#include "Tree.h"
#include "OrganismManager.h"
#include "TreeCommandProcessor.h"
#include "components/RigidBodyComponent.h"
#include "core/Cell.h"
#include "core/LoggingChannels.h"
#include "core/MaterialType.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {

Tree::Tree(
    OrganismId id, std::unique_ptr<TreeBrain> brain, std::unique_ptr<ITreeCommandProcessor> proc)
    : Organism::Body(id, OrganismType::TREE),
      processor(std::move(proc)),
      brain_(std::move(brain)),
      rigidBody_(std::make_unique<RigidBodyComponent>(Material::EnumType::Seed))
{
    // Initialize local shape with a single SEED cell at origin.
    rigidBody_->addCell({ 0, 0 }, Material::EnumType::Seed, 1.0);

    // Keep base class local_shape in sync for mass computation.
    local_shape.push_back(
        LocalCell{ .localPos = { 0, 0 }, .material = Material::EnumType::Seed, .fillRatio = 1.0 });

    recomputeMass();
    recomputeCenterOfMass();
}

Tree::~Tree() = default;

Vector2i Tree::getAnchorCell() const
{
    return Vector2i{ static_cast<int>(std::floor(position.x)),
                     static_cast<int>(std::floor(position.y)) };
}

void Tree::setAnchorCell(Vector2i pos)
{
    position.x = static_cast<double>(pos.x) + 0.5;
    position.y = static_cast<double>(pos.y) + 0.5;
}

void Tree::update(World& world, double deltaTime)
{
    age_seconds_ += deltaTime;

    // Tick down current command timer.
    if (current_command_.has_value()) {
        time_remaining_seconds_ -= deltaTime;
        if (time_remaining_seconds_ <= 0.0) {
            executeCommand(world);
            current_command_.reset();
        }
    }

    // Brain runs every tick - it can propose new commands or cancel current ones.
    processBrainDecision(world);

    updateResources(world);

    // Run rigid body physics (gravity, collision, ground support).
    // Trees don't have external forces (no walking), so just pass zero.
    auto result = rigidBody_->update(
        id_, position, velocity, mass, local_shape, world, deltaTime, Vector2d{ 0.0, 0.0 });

    // Sync cells from projection.
    occupied_cells = result.occupied_cells;
    cells_.clear();
    for (const auto& pos : occupied_cells) {
        cells_.insert(pos);
    }
}

void Tree::executeCommand(World& world)
{
    CommandExecutionResult result = processor->execute(*this, world, *current_command_);

    if (!result.succeeded()) {
        LOG_INFO(Brain, "Tree {}: {}", id_, result.message);
    }
}

void Tree::processBrainDecision(World& world)
{
    // Gather sensory data.
    TreeSensoryData sensory = gatherSensoryData(world);

    // Ask brain for decision.
    TreeCommand command = brain_->decide(sensory);

    // Handle the command.
    std::visit(
        [&](auto&& cmd) {
            using T = std::decay_t<decltype(cmd)>;

            if constexpr (std::is_same_v<T, WaitCommand>) {
                // Do nothing this tick.
            }
            else if constexpr (std::is_same_v<T, CancelCommand>) {
                // Cancel in-progress action.
                if (current_command_.has_value()) {
                    LOG_INFO(Brain, "Tree {}: Cancelled current action", id_);
                    current_command_.reset();
                    time_remaining_seconds_ = 0.0;
                    total_command_time_seconds_ = 0.0;
                }
            }
            else {
                // Action command - only start if idle.
                if (!current_command_.has_value()) {
                    current_command_ = cmd;
                    time_remaining_seconds_ = cmd.execution_time_seconds;
                    total_command_time_seconds_ = time_remaining_seconds_;
                }
            }
        },
        command);
}

void Tree::updateResources(const World& world)
{
    // TODO: Implement resource aggregation from cells.
    // For now, just placeholder values.
    (void)world;

    // Phase 3 will implement:
    // - Iterate over cells, sum energy and water from cell metadata.
    // - Calculate photosynthesis from LEAF cells based on light exposure.
    // - Extract nutrients from DIRT via ROOT cells.
    // - Deduct maintenance costs.
}

TreeSensoryData Tree::gatherSensoryData(const World& world) const
{
    TreeSensoryData data;

    // Find actual current cell positions by scanning world for organism_id.
    // This handles cells that have moved due to physics (falling seeds).
    int min_x = INT32_MAX, min_y = INT32_MAX;
    int max_x = INT32_MIN, max_y = INT32_MIN;
    int cell_count = 0;

    for (int16_t y = 0; y < world.getData().height; y++) {
        for (int16_t x = 0; x < world.getData().width; x++) {
            Vector2i pos{ x, y };
            if (world.getOrganismManager().at(pos) == id_) {
                min_x = std::min(min_x, static_cast<int>(x));
                min_y = std::min(min_y, static_cast<int>(y));
                max_x = std::max(max_x, static_cast<int>(x));
                max_y = std::max(max_y, static_cast<int>(y));
                cell_count++;
            }
        }
    }

    // No cells found - tree might have been destroyed.
    if (cell_count == 0) {
        data.actual_width = TreeSensoryData::GRID_SIZE;
        data.actual_height = TreeSensoryData::GRID_SIZE;
        data.scale_factor = 1.0;
        data.world_offset = Vector2i{ 0, 0 };
        return data;
    }

    int bbox_width = max_x - min_x + 1;
    int bbox_height = max_y - min_y + 1;

    // Small trees: Use fixed 15x15 viewing window centered on tree's current position (1:1
    // mapping).
    if (bbox_width <= TreeSensoryData::GRID_SIZE && bbox_height <= TreeSensoryData::GRID_SIZE) {
        data.actual_width = TreeSensoryData::GRID_SIZE;
        data.actual_height = TreeSensoryData::GRID_SIZE;
        data.scale_factor = 1.0;

        // Center the 15x15 window on the tree's current position.
        int half_window = TreeSensoryData::GRID_SIZE / 2; // 7 cells on each side.
        Vector2i anchor = getAnchorCell();
        int offset_x = anchor.x - half_window;
        int offset_y = anchor.y - half_window;

        // Clamp to world bounds (allow negative offsets for small worlds).
        // For worlds >= 15x15: clamp to keep window inside world.
        // For worlds < 15x15: allow negative offset to center seed in neural grid.
        if (static_cast<int>(world.getData().width) >= TreeSensoryData::GRID_SIZE) {
            offset_x = std::max(
                0,
                std::min(
                    static_cast<int>(world.getData().width) - TreeSensoryData::GRID_SIZE,
                    offset_x));
        }
        // else: leave offset_x as calculated (may be negative).

        if (static_cast<int>(world.getData().height) >= TreeSensoryData::GRID_SIZE) {
            offset_y = std::max(
                0,
                std::min(
                    static_cast<int>(world.getData().height) - TreeSensoryData::GRID_SIZE,
                    offset_y));
        }
        // else: leave offset_y as calculated (may be negative).

        data.world_offset = Vector2i{ offset_x, offset_y };
    }
    // Large trees: Use bounding box + padding, downsample to fit 15x15.
    else {
        // Add 1-cell padding.
        min_x = std::max(0, min_x - 1);
        min_y = std::max(0, min_y - 1);
        max_x = std::min(static_cast<int>(world.getData().width) - 1, max_x + 1);
        max_y = std::min(static_cast<int>(world.getData().height) - 1, max_y + 1);

        data.actual_width = max_x - min_x + 1;
        data.actual_height = max_y - min_y + 1;
        data.world_offset = Vector2i{ min_x, min_y };
        data.scale_factor = std::max(
            static_cast<double>(data.actual_width) / TreeSensoryData::GRID_SIZE,
            static_cast<double>(data.actual_height) / TreeSensoryData::GRID_SIZE);
    }

    // Populate material histograms by sampling world grid.
    for (int ny = 0; ny < TreeSensoryData::GRID_SIZE; ny++) {
        for (int nx = 0; nx < TreeSensoryData::GRID_SIZE; nx++) {
            // Map neural coords to world region.
            int wx_start = data.world_offset.x + static_cast<int>(nx * data.scale_factor);
            int wy_start = data.world_offset.y + static_cast<int>(ny * data.scale_factor);
            int wx_end = data.world_offset.x + static_cast<int>((nx + 1) * data.scale_factor);
            int wy_end = data.world_offset.y + static_cast<int>((ny + 1) * data.scale_factor);

            // Check if region is completely out of bounds (skip sampling to get empty histogram).
            if (wx_end <= 0 || wx_start >= static_cast<int>(world.getData().width) || wy_end <= 0
                || wy_start >= static_cast<int>(world.getData().height)) {
                // Completely OOB - leave histogram as zeros (will render as AIR/black).
                continue;
            }

            // Clamp to world bounds.
            wx_start = std::max(0, std::min(static_cast<int>(world.getData().width) - 1, wx_start));
            wy_start =
                std::max(0, std::min(static_cast<int>(world.getData().height) - 1, wy_start));
            wx_end = std::max(0, std::min(static_cast<int>(world.getData().width), wx_end));
            wy_end = std::max(0, std::min(static_cast<int>(world.getData().height), wy_end));

            // Count materials in this region.
            std::array<int, TreeSensoryData::NUM_MATERIALS> counts = {};
            int total_cells = 0;

            for (int wy = wy_start; wy < wy_end; wy++) {
                for (int wx = wx_start; wx < wx_end; wx++) {
                    const auto& cell = world.getData().at(wx, wy);
                    int mat_idx = static_cast<int>(cell.material_type);
                    if (mat_idx >= 0 && mat_idx < TreeSensoryData::NUM_MATERIALS) {
                        counts[mat_idx]++;
                        total_cells++;
                    }
                }
            }

            // Normalize to histogram probabilities.
            if (total_cells > 0) {
                for (int i = 0; i < TreeSensoryData::NUM_MATERIALS; i++) {
                    data.material_histograms[ny][nx][i] =
                        static_cast<double>(counts[i]) / total_cells;
                }
            }
        }
    }

    data.seed_position = getAnchorCell();
    data.age_seconds = age_seconds_;
    data.stage = stage_;
    data.total_energy = total_energy_;
    data.total_water = total_water_;

    // Current action state.
    if (current_command_.has_value()) {
        data.current_action = getCommandType(*current_command_);

        // Calculate progress (0.0 to 1.0).
        if (total_command_time_seconds_ > 0.0) {
            data.action_progress = 1.0 - (time_remaining_seconds_ / total_command_time_seconds_);
            data.action_progress = std::clamp(data.action_progress, 0.0, 1.0);
        }

        std::visit(
            [&](auto&& cmd) {
                using T = std::decay_t<decltype(cmd)>;
                if constexpr (std::is_same_v<T, WaitCommand>) {
                    data.current_thought = "Waiting";
                }
                else if constexpr (std::is_same_v<T, CancelCommand>) {
                    data.current_thought = "Cancelling";
                }
                else if constexpr (std::is_same_v<T, GrowWoodCommand>) {
                    data.current_thought = "Growing WOOD at (" + std::to_string(cmd.target_pos.x)
                        + ", " + std::to_string(cmd.target_pos.y) + ")";
                }
                else if constexpr (std::is_same_v<T, GrowLeafCommand>) {
                    data.current_thought = "Growing LEAF at (" + std::to_string(cmd.target_pos.x)
                        + ", " + std::to_string(cmd.target_pos.y) + ")";
                }
                else if constexpr (std::is_same_v<T, GrowRootCommand>) {
                    data.current_thought = "Growing ROOT at (" + std::to_string(cmd.target_pos.x)
                        + ", " + std::to_string(cmd.target_pos.y) + ")";
                }
                else if constexpr (std::is_same_v<T, ReinforceCellCommand>) {
                    data.current_thought = "Reinforcing cell at (" + std::to_string(cmd.position.x)
                        + ", " + std::to_string(cmd.position.y) + ")";
                }
                else if constexpr (std::is_same_v<T, ProduceSeedCommand>) {
                    data.current_thought = "Producing SEED at (" + std::to_string(cmd.position.x)
                        + ", " + std::to_string(cmd.position.y) + ")";
                }
            },
            *current_command_);
    }
    else {
        data.current_action = std::nullopt;
        data.action_progress = 0.0;
        data.current_thought = "Idle";
    }

    return data;
}

void Tree::addCellToLocalShape(Vector2i localPos, Material::EnumType material, double fillRatio)
{
    // Add to RigidBodyComponent projection.
    rigidBody_->addCell(localPos, material, fillRatio);

    // Add to base class local_shape for mass computation.
    local_shape.push_back(
        LocalCell{ .localPos = localPos, .material = material, .fillRatio = fillRatio });

    // Recompute mass and center of mass.
    recomputeMass();
    recomputeCenterOfMass();
}

} // namespace DirtSim
