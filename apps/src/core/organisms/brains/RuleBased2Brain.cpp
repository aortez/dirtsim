#include "RuleBased2Brain.h"
#include "core/MaterialType.h"
#include <algorithm>

namespace DirtSim {

namespace {
constexpr double kObservationSeconds = 2.0;
constexpr int kCellsPerRoot = 3;
constexpr double kEnergyCostWood = 10.0;
constexpr double kEnergyCostLeaf = 8.0;
constexpr double kEnergyCostRoot = 12.0;
} // namespace

TreeCommand RuleBased2Brain::decide(const TreeSensoryData& sensory)
{
    if (sensory.current_action.has_value()) {
        return WaitCommand{};
    }

    if (sensory.stage == GrowthStage::SEED) {
        const Vector2i seed = sensory.seed_position;
        const Vector2i directions[] = { { 0, 1 },  { 0, -1 }, { -1, 0 },  { 1, 0 },
                                        { -1, 1 }, { 1, 1 },  { -1, -1 }, { 1, -1 } };
        const int dirt_idx = static_cast<int>(Material::EnumType::Dirt);

        auto isDirtContact = [&](Vector2i world_pos) {
            if (!isInSensoryGrid(sensory, world_pos)) {
                return false;
            }

            const int grid_x = world_pos.x - sensory.world_offset.x;
            const int grid_y = world_pos.y - sensory.world_offset.y;
            return sensory.material_histograms[grid_y][grid_x][dirt_idx] > 0.5;
        };

        if (!has_contacted_dirt_ || !isDirtContact(root_target_pos_)) {
            bool found_contact = false;
            for (const auto& dir : directions) {
                Vector2i check_pos = seed + dir;
                if (!isDirtContact(check_pos)) {
                    continue;
                }

                has_contacted_dirt_ = true;
                dirt_contact_age_seconds_ = sensory.age_seconds;
                root_target_pos_ = check_pos;
                found_contact = true;
                break;
            }

            if (!found_contact) {
                has_contacted_dirt_ = false;
                return WaitCommand{};
            }
        }

        double observation_time = sensory.age_seconds - dirt_contact_age_seconds_;
        if (observation_time >= kObservationSeconds) {
            if (isGrowthSuitable(sensory, root_target_pos_, Material::EnumType::Root)) {
                return GrowRootCommand{ .target_pos = root_target_pos_,
                                        .execution_time_seconds = 2.0 };
            }
            has_contacted_dirt_ = false;
        }

        return WaitCommand{};
    }

    if (sensory.stage == GrowthStage::GERMINATION) {
        const int trunk_height = countTrunkHeight(sensory);
        if (trunk_height == 0) {
            Vector2i wood_pos{ sensory.seed_position.x, sensory.seed_position.y - 1 };
            if (isGrowthSuitable(sensory, wood_pos, Material::EnumType::Wood)) {
                return GrowWoodCommand{ .target_pos = wood_pos, .execution_time_seconds = 3.0 };
            }
        }

        return WaitCommand{};
    }

    const TreeCounts counts = analyzeTreeCounts(sensory);
    const int trunk_height = countTrunkHeight(sensory);
    const int above_ground_cells = counts.wood_count + counts.leaf_count + 1;
    const int root_capacity = counts.root_count * kCellsPerRoot;

    const bool can_afford_leaf = sensory.total_energy >= kEnergyCostLeaf;
    const bool can_afford_wood = sensory.total_energy >= kEnergyCostWood;
    const bool can_afford_root = sensory.total_energy >= kEnergyCostRoot;

    if (above_ground_cells > root_capacity && can_afford_root) {
        Vector2i pos = findRootGrowthPosition(sensory);
        if (isGrowthSuitable(sensory, pos, Material::EnumType::Root)) {
            return GrowRootCommand{ .target_pos = pos, .execution_time_seconds = 2.0 };
        }
    }

    if (can_afford_leaf && counts.leaf_count == 0) {
        Vector2i pos = findLeafGrowthPosition(sensory);
        if (isGrowthSuitable(sensory, pos, Material::EnumType::Leaf)) {
            return GrowLeafCommand{ .target_pos = pos, .execution_time_seconds = 0.5 };
        }
    }

    if (trunk_height < 3 && can_afford_wood) {
        Vector2i pos = findTrunkGrowthPosition(sensory, trunk_height);
        if (isGrowthSuitable(sensory, pos, Material::EnumType::Wood)) {
            return GrowWoodCommand{ .target_pos = pos, .execution_time_seconds = 3.0 };
        }
    }

    const double total_cells = counts.total_cells > 0 ? counts.total_cells : 1.0;
    const double leaf_ratio = counts.leaf_count / total_cells;
    if (can_afford_leaf && leaf_ratio < 0.35) {
        Vector2i pos = findLeafGrowthPosition(sensory);
        if (isGrowthSuitable(sensory, pos, Material::EnumType::Leaf)) {
            return GrowLeafCommand{ .target_pos = pos, .execution_time_seconds = 0.5 };
        }
    }

    if (trunk_height >= 3 && can_afford_wood) {
        Vector2i pos = findBranchStartPosition(sensory, trunk_height);
        if (isGrowthSuitable(sensory, pos, Material::EnumType::Wood)) {
            return GrowWoodCommand{ .target_pos = pos, .execution_time_seconds = 3.0 };
        }
    }

    if (can_afford_wood) {
        Vector2i pos = findTrunkGrowthPosition(sensory, trunk_height);
        if (isGrowthSuitable(sensory, pos, Material::EnumType::Wood)) {
            return GrowWoodCommand{ .target_pos = pos, .execution_time_seconds = 3.0 };
        }
    }

    if (can_afford_leaf) {
        Vector2i pos = findLeafGrowthPosition(sensory);
        if (isGrowthSuitable(sensory, pos, Material::EnumType::Leaf)) {
            return GrowLeafCommand{ .target_pos = pos, .execution_time_seconds = 0.5 };
        }
    }

    return WaitCommand{};
}

RuleBased2Brain::TreeCounts RuleBased2Brain::analyzeTreeCounts(const TreeSensoryData& sensory) const
{
    TreeCounts counts;
    const int root_idx = static_cast<int>(Material::EnumType::Root);
    const int wood_idx = static_cast<int>(Material::EnumType::Wood);
    const int leaf_idx = static_cast<int>(Material::EnumType::Leaf);

    for (int y = 0; y < sensory.GRID_SIZE; y++) {
        for (int x = 0; x < sensory.GRID_SIZE; x++) {
            const auto& hist = sensory.material_histograms[y][x];
            if (hist[root_idx] > 0.5) {
                counts.root_count++;
            }
            if (hist[wood_idx] > 0.5) {
                counts.wood_count++;
            }
            if (hist[leaf_idx] > 0.5) {
                counts.leaf_count++;
            }
        }
    }

    counts.total_cells = counts.root_count + counts.wood_count + counts.leaf_count;
    return counts;
}

int RuleBased2Brain::countTrunkHeight(const TreeSensoryData& sensory) const
{
    int height = 0;
    Vector2i seed = sensory.seed_position;
    const int wood_idx = static_cast<int>(Material::EnumType::Wood);

    for (int step = 1; step < sensory.GRID_SIZE; step++) {
        Vector2i pos{ seed.x, seed.y - step };
        if (!isInSensoryGrid(sensory, pos)) {
            break;
        }

        const int grid_x = pos.x - sensory.world_offset.x;
        const int grid_y = pos.y - sensory.world_offset.y;
        if (sensory.material_histograms[grid_y][grid_x][wood_idx] > 0.5) {
            height++;
        }
        else {
            break;
        }
    }

    return height;
}

bool RuleBased2Brain::isTrunkColumn(const TreeSensoryData& sensory, Vector2i world_pos) const
{
    return world_pos.x == sensory.seed_position.x && world_pos.y < sensory.seed_position.y;
}

bool RuleBased2Brain::isInSensoryGrid(const TreeSensoryData& sensory, Vector2i world_pos) const
{
    int grid_x = world_pos.x - sensory.world_offset.x;
    int grid_y = world_pos.y - sensory.world_offset.y;
    return grid_x >= 0 && grid_y >= 0 && grid_x < sensory.GRID_SIZE && grid_y < sensory.GRID_SIZE;
}

bool RuleBased2Brain::isGrowthSuitable(
    const TreeSensoryData& sensory, Vector2i world_pos, Material::EnumType target_material) const
{
    if (!isInSensoryGrid(sensory, world_pos)) {
        return false;
    }

    const int grid_x = world_pos.x - sensory.world_offset.x;
    const int grid_y = world_pos.y - sensory.world_offset.y;
    const auto& histogram = sensory.material_histograms[grid_y][grid_x];

    double air = histogram[static_cast<int>(Material::EnumType::Air)];
    double dirt = histogram[static_cast<int>(Material::EnumType::Dirt)];
    double sand = histogram[static_cast<int>(Material::EnumType::Sand)];
    double water = histogram[static_cast<int>(Material::EnumType::Water)];
    double wall = histogram[static_cast<int>(Material::EnumType::Wall)];
    double metal = histogram[static_cast<int>(Material::EnumType::Metal)];

    if (wall > 0.5 || metal > 0.5 || water > 0.5) {
        return false;
    }

    if (target_material == Material::EnumType::Leaf) {
        return air > 0.5;
    }

    return air > 0.3 || dirt > 0.3 || sand > 0.3;
}

Vector2i RuleBased2Brain::findRootGrowthPosition(const TreeSensoryData& sensory) const
{
    const int root_idx = static_cast<int>(Material::EnumType::Root);
    Vector2i best_pos = sensory.seed_position;
    int best_depth = INT32_MIN;

    for (int y = 0; y < sensory.GRID_SIZE; y++) {
        for (int x = 0; x < sensory.GRID_SIZE; x++) {
            if (sensory.material_histograms[y][x][root_idx] <= 0.5) {
                continue;
            }

            Vector2i root_pos = sensory.world_offset + Vector2i{ x, y };
            Vector2i directions[] = { { 0, 1 }, { -1, 0 }, { 1, 0 } };
            for (const auto& dir : directions) {
                Vector2i candidate = root_pos + dir;
                if (!isGrowthSuitable(sensory, candidate, Material::EnumType::Root)) {
                    continue;
                }

                if (candidate.y > best_depth) {
                    best_depth = candidate.y;
                    best_pos = candidate;
                }
            }
        }
    }

    return best_pos;
}

Vector2i RuleBased2Brain::findTrunkGrowthPosition(
    const TreeSensoryData& sensory, int trunk_height) const
{
    Vector2i seed = sensory.seed_position;
    return seed + Vector2i{ 0, -trunk_height - 1 };
}

Vector2i RuleBased2Brain::findLeafGrowthPosition(const TreeSensoryData& sensory) const
{
    Vector2i seed = sensory.seed_position;
    const int wood_idx = static_cast<int>(Material::EnumType::Wood);
    Vector2i best_pos = seed;
    double best_score = -1.0;

    for (int y = 0; y < sensory.GRID_SIZE; y++) {
        for (int x = 0; x < sensory.GRID_SIZE; x++) {
            if (sensory.material_histograms[y][x][wood_idx] <= 0.5) {
                continue;
            }

            Vector2i wood_pos = sensory.world_offset + Vector2i{ x, y };
            Vector2i directions[] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
            for (const auto& dir : directions) {
                Vector2i candidate = wood_pos + dir;
                if (candidate.y >= seed.y) {
                    continue;
                }
                if (isTrunkColumn(sensory, candidate)) {
                    continue;
                }
                if (!isGrowthSuitable(sensory, candidate, Material::EnumType::Leaf)) {
                    continue;
                }

                const int dx = candidate.x - seed.x;
                const int dy = candidate.y - seed.y;
                const double score = dx * dx + dy * dy;
                if (score > best_score) {
                    best_score = score;
                    best_pos = candidate;
                }
            }
        }
    }

    return best_pos;
}

Vector2i RuleBased2Brain::findBranchStartPosition(
    const TreeSensoryData& sensory, int trunk_height) const
{
    Vector2i seed = sensory.seed_position;
    int top_y = seed.y - trunk_height;

    for (int y = top_y; y < seed.y; y++) {
        Vector2i left{ seed.x - 1, y };
        Vector2i right{ seed.x + 1, y };

        const bool left_ok = isGrowthSuitable(sensory, left, Material::EnumType::Wood);
        const bool right_ok = isGrowthSuitable(sensory, right, Material::EnumType::Wood);

        if (left_ok && right_ok) {
            return left;
        }
        if (left_ok) {
            return left;
        }
        if (right_ok) {
            return right;
        }
    }

    return seed;
}

} // namespace DirtSim
