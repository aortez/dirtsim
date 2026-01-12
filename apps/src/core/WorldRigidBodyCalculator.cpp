#include "WorldRigidBodyCalculator.h"
#include "Cell.h"
#include "World.h"
#include "WorldData.h"
#include "organisms/OrganismManager.h"
#include <queue>
#include <unordered_set>

using namespace DirtSim;

namespace {

struct Vector2iHash {
    size_t operator()(const Vector2i& v) const
    {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 16);
    }
};

} // namespace

RigidStructure WorldRigidBodyCalculator::findConnectedStructure(
    const World& world, Vector2i start, OrganismId organism_id) const
{
    RigidStructure result;
    const auto& data = world.getData();

    if (!data.inBounds(start.x, start.y)) {
        return result;
    }

    // Structures are organism-only.
    OrganismId start_org = world.getOrganismManager().at(start);
    if (start_org == INVALID_ORGANISM_ID) {
        return result;
    }

    // If organism_id specified, start cell must match.
    if (organism_id != INVALID_ORGANISM_ID && start_org != organism_id) {
        return result;
    }

    OrganismId match_organism = organism_id != INVALID_ORGANISM_ID ? organism_id : start_org;

    std::unordered_set<Vector2i, Vector2iHash> visited;
    std::queue<Vector2i> frontier;

    frontier.push(start);
    visited.insert(start);

    while (!frontier.empty()) {
        Vector2i pos = frontier.front();
        frontier.pop();
        result.cells.push_back(pos);

        // Check 4-connected neighbors.
        const Vector2i directions[] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
        for (const auto& dir : directions) {
            Vector2i neighbor = { pos.x + dir.x, pos.y + dir.y };

            if (!data.inBounds(neighbor.x, neighbor.y)) {
                continue;
            }
            if (visited.count(neighbor)) {
                continue;
            }

            // Only connect cells with same organism_id.
            if (world.getOrganismManager().at(neighbor) != match_organism) {
                continue;
            }

            visited.insert(neighbor);
            frontier.push(neighbor);
        }
    }

    // Calculate mass, COM, and initial velocity for the found structure.
    result.total_mass = calculateStructureMass(world, result);
    result.center_of_mass = calculateStructureCOM(world, result);
    result.organism_id = match_organism;

    // Initialize velocity from first cell (all cells should have same velocity after first frame).
    if (!result.empty()) {
        const Cell& first_cell = world.getData().at(result.cells[0].x, result.cells[0].y);
        result.velocity = first_cell.velocity;
    }

    return result;
}

std::vector<RigidStructure> WorldRigidBodyCalculator::findAllStructures(const World& world) const
{
    std::vector<RigidStructure> structures;
    const auto& data = world.getData();
    std::unordered_set<Vector2i, Vector2iHash> processed;

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            Vector2i pos{ x, y };
            if (processed.count(pos)) {
                continue;
            }

            // Structures are organism-only.
            OrganismId org_id = world.getOrganismManager().at(pos);
            if (org_id == INVALID_ORGANISM_ID) {
                continue;
            }

            RigidStructure structure = findConnectedStructure(world, pos, org_id);
            if (!structure.empty()) {
                for (const auto& cell_pos : structure.cells) {
                    processed.insert(cell_pos);
                }
                structures.push_back(std::move(structure));
            }
        }
    }

    return structures;
}

Vector2d WorldRigidBodyCalculator::calculateStructureCOM(
    const World& world, const RigidStructure& structure) const
{
    if (structure.empty()) {
        return {};
    }

    const auto& data = world.getData();
    Vector2d weighted_sum;
    double total_mass = 0.0;

    for (const auto& pos : structure.cells) {
        const Cell& cell = data.at(pos.x, pos.y);
        double mass = cell.getMass();

        // World position = cell grid position + COM offset (COM is in [-1,1]).
        Vector2d world_pos{ static_cast<double>(pos.x) + cell.com.x * 0.5,
                            static_cast<double>(pos.y) + cell.com.y * 0.5 };

        weighted_sum.x += world_pos.x * mass;
        weighted_sum.y += world_pos.y * mass;
        total_mass += mass;
    }

    if (total_mass > 0) {
        return { weighted_sum.x / total_mass, weighted_sum.y / total_mass };
    }
    return {};
}

double WorldRigidBodyCalculator::calculateStructureMass(
    const World& world, const RigidStructure& structure) const
{
    const auto& data = world.getData();
    double total = 0.0;

    for (const auto& pos : structure.cells) {
        total += data.at(pos.x, pos.y).getMass();
    }

    return total;
}

Vector2d WorldRigidBodyCalculator::gatherStructureForces(
    const World& world, const RigidStructure& structure) const
{
    const auto& data = world.getData();
    Vector2d net_force;

    for (const auto& pos : structure.cells) {
        const Cell& cell = data.at(pos.x, pos.y);
        net_force.x += cell.pending_force.x;
        net_force.y += cell.pending_force.y;
    }

    return net_force;
}

void WorldRigidBodyCalculator::applyUnifiedVelocity(
    World& world, RigidStructure& structure, double deltaTime) const
{
    if (structure.empty() || structure.total_mass < 0.0001) {
        return;
    }

    // Gather total force acting on structure.
    Vector2d total_force = gatherStructureForces(world, structure);

    // F = ma → a = F/m.
    Vector2d acceleration = total_force * (1.0 / structure.total_mass);

    // Update structure velocity.
    structure.velocity.x += acceleration.x * deltaTime;
    structure.velocity.y += acceleration.y * deltaTime;

    // Apply unified velocity to all cells in structure.
    auto& data = world.getData();
    for (const auto& pos : structure.cells) {
        Cell& cell = data.at(pos.x, pos.y);
        cell.velocity = structure.velocity;
    }
}
