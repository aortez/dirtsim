#pragma once

#include "MaterialType.h"
#include "Vector2d.h"
#include "Vector2i.h"
#include "WorldCalculatorBase.h"
#include <cstdint>
#include <vector>

namespace DirtSim {

class World;

struct RigidStructure {
    std::vector<Vector2i> cells;
    Vector2d center_of_mass;
    double total_mass = 0.0;
    Vector2d velocity;
    uint32_t organism_id = 0;

    bool empty() const { return cells.empty(); }
    size_t size() const { return cells.size(); }
};

class WorldRigidBodyCalculator : public WorldCalculatorBase {
public:
    WorldRigidBodyCalculator() = default;

    RigidStructure findConnectedStructure(
        const World& world, Vector2i start, uint32_t organism_id = 0) const;

    std::vector<RigidStructure> findAllStructures(const World& world) const;

    Vector2d calculateStructureCOM(const World& world, const RigidStructure& structure) const;

    double calculateStructureMass(const World& world, const RigidStructure& structure) const;

    Vector2d gatherStructureForces(const World& world, const RigidStructure& structure) const;

    void applyUnifiedVelocity(World& world, RigidStructure& structure, double deltaTime) const;
};

} // namespace DirtSim
