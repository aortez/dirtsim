#pragma once

#include "Tree.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace DirtSim {

struct OrganismTransfer {
    Vector2i from_pos;
    Vector2i to_pos;
    TreeId organism_id;
    double amount;
};

class World;

class TreeManager {
public:
    TreeManager() = default;

    void update(World& world, double deltaTime);
    TreeId plantSeed(World& world, uint32_t x, uint32_t y);
    void removeTree(TreeId id);
    void clear();

    Tree* getTree(TreeId id);
    const Tree* getTree(TreeId id) const;
    TreeId getTreeAtCell(const Vector2i& pos) const;

    const std::unordered_map<TreeId, Tree>& getTrees() const { return trees_; }

    void notifyTransfers(const std::vector<OrganismTransfer>& transfers);

    void applyBoneForces(World& world, double deltaTime);

    // Remove cells from a tree's tracking (used when pruning disconnected fragments).
    // Does NOT modify cell.organism_id - caller is responsible for that.
    void removeCellsFromTree(TreeId tree_id, const std::vector<Vector2i>& positions);

    // Add a cell to a tree's tracking and set organism_id on the cell.
    // Used for growth and testing.
    void addCellToTree(World& world, TreeId tree_id, Vector2i pos);

private:
    std::unordered_map<TreeId, Tree> trees_;
    std::unordered_map<Vector2i, TreeId> cell_to_tree_;
    uint32_t next_tree_id_ = 1;
};

} // namespace DirtSim
