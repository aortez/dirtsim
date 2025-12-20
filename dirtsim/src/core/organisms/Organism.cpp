#include "Organism.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {

double getBoneStiffness(MaterialType a, MaterialType b)
{
    // Order-independent lookup via sorting.
    if (a > b) std::swap(a, b);

    // Core structure - very stiff.
    if ((a == MaterialType::SEED && b == MaterialType::WOOD)
        || (a == MaterialType::SEED && b == MaterialType::ROOT)) {
        return 1.0;
    }

    // Trunk and branches.
    if (a == MaterialType::WOOD && b == MaterialType::WOOD) {
        return 0.8;
    }

    // Root system - somewhat flexible.
    if (a == MaterialType::ROOT && b == MaterialType::ROOT) {
        return 0.5;
    }
    if (a == MaterialType::ROOT && b == MaterialType::WOOD) {
        return 0.6;
    }

    // Foliage - stiff attachment to wood, flexible between leaves.
    if (a == MaterialType::LEAF && b == MaterialType::WOOD) {
        return 3.0; // Strong attachment to prevent leaves from falling.
    }
    if (a == MaterialType::LEAF && b == MaterialType::LEAF) {
        return 0.1;
    }

    // Default for any other organism material pairs.
    return 0.3;
}

Organism::Organism(OrganismId id, OrganismType type)
    : id_(id)
    , type_(type)
{
}

void Organism::onCellTransfer(Vector2i from, Vector2i to)
{
    // Update anchor if it moved.
    if (from == getAnchorCell()) {
        setAnchorCell(to);
        spdlog::debug(
            "Organism {}: Anchor moved from ({}, {}) to ({}, {})",
            id_,
            from.x,
            from.y,
            to.x,
            to.y);
    }

    // Update bone endpoints when cells move.
    for (Bone& bone : bones_) {
        if (bone.cell_a == from) {
            bone.cell_a = to;
        }
        if (bone.cell_b == from) {
            bone.cell_b = to;
        }
    }
}

void Organism::createBonesForCell(Vector2i new_cell, MaterialType material, const World& world)
{
    // Bones disabled during rigid body implementation. The rigid body system provides
    // structural integrity for organisms without per-cell spring forces.
    (void)new_cell;
    (void)material;
    (void)world;
    return;

    const WorldData& data = world.getData();
    int bones_created = 0;

    spdlog::debug(
        "Organism {}: createBonesForCell for {} at ({},{})",
        id_,
        getMaterialName(material),
        new_cell.x,
        new_cell.y);

    // Check cardinal (non-diagonal) neighbors for cells belonging to this organism.
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue; // Skip self.

            // Skip diagonal neighbors - only cardinal directions.
            if (dx != 0 && dy != 0) continue;

            int nx = new_cell.x + dx;
            int ny = new_cell.y + dy;

            if (nx < 0 || ny < 0 || static_cast<uint32_t>(nx) >= data.width
                || static_cast<uint32_t>(ny) >= data.height) {
                continue;
            }

            const Cell& neighbor = data.at(nx, ny);

            if (neighbor.organism_id != id_) continue;

            Vector2i neighbor_pos{ nx, ny };
            double rest_dist = 1.0; // Cardinal neighbors are always distance 1.
            double stiffness = getBoneStiffness(material, neighbor.material_type);

            // Determine hinge point and rotational damping.
            HingeEnd hinge = HingeEnd::NONE;
            double rot_damping = 0.0;

            // For leaf-wood connections, wood is the hinge (leaves swing around branches).
            if (material == MaterialType::LEAF && neighbor.material_type == MaterialType::WOOD) {
                hinge = HingeEnd::CELL_B; // Neighbor (wood) is the pivot.
                rot_damping = 1.0;        // Passive damping to prevent leaf swinging.
            }
            else if (
                material == MaterialType::WOOD && neighbor.material_type == MaterialType::LEAF) {
                hinge = HingeEnd::CELL_A; // New cell (wood) is the pivot.
                rot_damping = 1.0;        // Passive damping to prevent leaf swinging.
            }
            // Other bone types remain symmetric springs.

            bones_.push_back(
                Bone{ new_cell, neighbor_pos, rest_dist, stiffness, hinge, rot_damping });
            bones_created++;

            spdlog::debug(
                "Organism {}: Created bone ({},{}) <-> ({},{}) rest={:.2f} stiff={:.2f}",
                id_,
                new_cell.x,
                new_cell.y,
                neighbor_pos.x,
                neighbor_pos.y,
                rest_dist,
                stiffness);
        }
    }

    if (bones_created == 0) {
        spdlog::debug(
            "Organism {}: No bones created for {} at ({},{}) - no adjacent organism cells",
            id_,
            getMaterialName(material),
            new_cell.x,
            new_cell.y);
    }
}

} // namespace DirtSim
