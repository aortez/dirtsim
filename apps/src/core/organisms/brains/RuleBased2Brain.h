#pragma once

#include "core/MaterialType.h"
#include "core/organisms/TreeBrain.h"

namespace DirtSim {

class RuleBased2Brain : public TreeBrain {
public:
    TreeCommand decide(const TreeSensoryData& sensory) override;

private:
    struct TreeCounts {
        int root_count = 0;
        int wood_count = 0;
        int leaf_count = 0;
        int total_cells = 0;
    };

    bool has_contacted_dirt_ = false;
    double dirt_contact_age_seconds_ = 0.0;
    Vector2i root_target_pos_;

    TreeCounts analyzeTreeCounts(const TreeSensoryData& sensory) const;
    int countTrunkHeight(const TreeSensoryData& sensory) const;
    bool isTrunkColumn(const TreeSensoryData& sensory, Vector2i world_pos) const;
    bool isInSensoryGrid(const TreeSensoryData& sensory, Vector2i world_pos) const;
    bool isGrowthSuitable(
        const TreeSensoryData& sensory,
        Vector2i world_pos,
        Material::EnumType target_material) const;

    Vector2i findRootGrowthPosition(const TreeSensoryData& sensory) const;
    Vector2i findTrunkGrowthPosition(const TreeSensoryData& sensory, int trunk_height) const;
    Vector2i findLeafGrowthPosition(const TreeSensoryData& sensory) const;
    Vector2i findBranchStartPosition(const TreeSensoryData& sensory, int trunk_height) const;
};

} // namespace DirtSim
