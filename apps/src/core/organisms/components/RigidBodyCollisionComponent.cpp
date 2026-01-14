#include "RigidBodyCollisionComponent.h"
#include "core/Cell.h"
#include "core/World.h"
#include "core/WorldData.h"
#include "core/organisms/OrganismManager.h"
#include <cmath>

namespace DirtSim {

CollisionResult RigidBodyCollisionComponent::detect(
    const World& world,
    OrganismId organismId,
    const std::vector<Vector2i>& currentCells,
    const std::vector<Vector2i>& predictedCells)
{
    CollisionResult result;
    const WorldData& data = world.getData();

    // Get reference current cell for normal calculation.
    // For multi-cell organisms, use the center of current cells.
    Vector2i currentRef{ 0, 0 };
    if (!currentCells.empty()) {
        for (const auto& cell : currentCells) {
            currentRef.x += cell.x;
            currentRef.y += cell.y;
        }
        currentRef.x /= static_cast<int>(currentCells.size());
        currentRef.y /= static_cast<int>(currentCells.size());
    }

    Vector2d normalSum{ 0.0, 0.0 };

    // Helper lambda to add normal based on which cell boundary was crossed.
    // Compares blocked cell to current cell position to determine collision direction.
    auto addBoundaryCrossedNormal = [&normalSum, &currentRef](const Vector2i& blockedPos) {
        int dx = blockedPos.x - currentRef.x;
        int dy = blockedPos.y - currentRef.y;

        // Determine collision direction based on which axis changed.
        // When both axes change equally (diagonal movement), prioritize vertical (floor/ceiling)
        // because organisms walking on floors commonly cross cell boundaries diagonally.
        if (std::abs(dy) >= std::abs(dx) && dy != 0) {
            // Vertical boundary crossed (floor or ceiling).
            normalSum.y -= (dy > 0) ? 1.0 : -1.0;
        }
        else if (dx != 0) {
            // Horizontal boundary crossed (left or right wall).
            normalSum.x -= (dx > 0) ? 1.0 : -1.0;
        }
        else {
            // Same cell (shouldn't happen) - use floor collision as default.
            normalSum.y -= 1.0;
        }
    };

    for (const auto& cellPos : predictedCells) {
        // Check world boundaries.
        if (!data.inBounds(cellPos.x, cellPos.y)) {
            result.blocked = true;
            result.blockedCells.push_back(cellPos);

            // Boundary normal points inward.
            if (cellPos.x < 0) {
                normalSum.x += 1.0;
            }
            if (cellPos.x >= data.width) {
                normalSum.x -= 1.0;
            }
            if (cellPos.y < 0) {
                normalSum.y += 1.0;
            }
            if (cellPos.y >= data.height) {
                normalSum.y -= 1.0;
            }
            continue;
        }

        const Cell& cell = data.at(cellPos.x, cellPos.y);

        // Check for WALL.
        if (cell.material_type == Material::EnumType::Wall) {
            result.blocked = true;
            result.blockedCells.push_back(cellPos);
            addBoundaryCrossedNormal(cellPos);
            continue;
        }

        // Check for other organism.
        OrganismId cellOrg = world.getOrganismManager().at(cellPos);
        if (cellOrg != INVALID_ORGANISM_ID && cellOrg != organismId) {
            result.blocked = true;
            result.blockedCells.push_back(cellPos);
            addBoundaryCrossedNormal(cellPos);
            continue;
        }

        // Check for dense solid material (not owned by this organism).
        bool isSolid = cell.material_type == Material::EnumType::Dirt
            || cell.material_type == Material::EnumType::Sand
            || cell.material_type == Material::EnumType::Wood
            || cell.material_type == Material::EnumType::Metal
            || cell.material_type == Material::EnumType::Root;

        if (isSolid && cell.fill_ratio > 0.8 && cellOrg != organismId) {
            result.blocked = true;
            result.blockedCells.push_back(cellPos);
            addBoundaryCrossedNormal(cellPos);
            continue;
        }
    }

    // Normalize the accumulated contact normal.
    if (result.blocked) {
        double len = std::sqrt(normalSum.x * normalSum.x + normalSum.y * normalSum.y);
        if (len > 0.0001) {
            result.contactNormal.x = normalSum.x / len;
            result.contactNormal.y = normalSum.y / len;
        }
    }

    return result;
}

void RigidBodyCollisionComponent::respond(
    const CollisionResult& collision, Vector2d& velocity, double restitution)
{
    if (!collision.blocked) {
        return;
    }

    const Vector2d& normal = collision.contactNormal;

    // Zero normal means we couldn't determine direction - don't modify velocity.
    if (normal.x == 0.0 && normal.y == 0.0) {
        return;
    }

    // Compute velocity component into the surface.
    double vIntoSurface = velocity.x * normal.x + velocity.y * normal.y;

    // Only respond if moving into the surface.
    if (vIntoSurface >= 0.0) {
        return;
    }

    // Remove velocity into surface, optionally add bounce.
    double impulse = -vIntoSurface * (1.0 + restitution);
    velocity.x += impulse * normal.x;
    velocity.y += impulse * normal.y;
}

Vector2d RigidBodyCollisionComponent::computeSupportForce(
    const World& world,
    OrganismId organismId,
    const std::vector<Vector2i>& currentCells,
    double weight,
    Vector2d gravityDir)
{
    if (currentCells.empty() || weight < 0.0001) {
        return { 0.0, 0.0 };
    }

    const WorldData& data = world.getData();
    int contactCount = 0;
    double supportFraction = 0.0;

    for (const auto& pos : currentCells) {
        // Check cell below (in gravity direction).
        int groundX = pos.x + static_cast<int>(gravityDir.x);
        int groundY = pos.y + static_cast<int>(gravityDir.y);

        // World boundary = full support.
        if (!data.inBounds(groundX, groundY)) {
            return { -gravityDir.x * weight, -gravityDir.y * weight };
        }

        const Cell& groundCell = data.at(groundX, groundY);

        // Skip empty cells.
        if (groundCell.isEmpty()) {
            continue;
        }

        // Skip cells belonging to same organism.
        Vector2i groundPos{ groundX, groundY };
        if (world.getOrganismManager().at(groundPos) == organismId) {
            continue;
        }

        ++contactCount;

        // Solid materials provide full support.
        Material::EnumType mat = groundCell.material_type;
        if (mat == Material::EnumType::Wall || mat == Material::EnumType::Metal
            || mat == Material::EnumType::Wood || mat == Material::EnumType::Dirt
            || mat == Material::EnumType::Sand || mat == Material::EnumType::Seed
            || mat == Material::EnumType::Root) {
            supportFraction += 1.0;
        }
        else if (mat == Material::EnumType::Water) {
            // Partial buoyancy.
            supportFraction += 0.5 * groundCell.fill_ratio;
        }
        else if (mat == Material::EnumType::Leaf) {
            supportFraction += 0.3 * groundCell.fill_ratio;
        }
    }

    // No contact = free fall.
    if (contactCount == 0) {
        return { 0.0, 0.0 };
    }

    // Any solid contact provides full support.
    double normalized = std::min(supportFraction / static_cast<double>(contactCount), 1.0);
    if (normalized > 0.5) {
        normalized = 1.0;
    }

    double magnitude = weight * normalized;
    return { -gravityDir.x * magnitude, -gravityDir.y * magnitude };
}

Vector2d RigidBodyCollisionComponent::computeGroundFriction(
    const World& world,
    OrganismId organismId,
    const std::vector<Vector2i>& currentCells,
    const Vector2d& velocity,
    double normalForce)
{
    // No ground contact = no friction.
    if (normalForce < 0.01 || currentCells.empty()) {
        return { 0.0, 0.0 };
    }

    // Extract horizontal velocity (tangential to ground).
    // Assumes horizontal ground (gravity downward).
    Vector2d tangentialVelocity{ velocity.x, 0.0 };
    double tangentialSpeed = std::abs(tangentialVelocity.x);

    // No motion = no friction force.
    static constexpr double MIN_TANGENTIAL_SPEED = 1e-6;
    if (tangentialSpeed < MIN_TANGENTIAL_SPEED) {
        return { 0.0, 0.0 };
    }

    // Find ground materials below organism.
    const WorldData& data = world.getData();
    Vector2d gravityDir{ 0.0, 1.0 }; // Assumes downward gravity.

    std::vector<Material::EnumType> groundMaterials;
    for (const auto& pos : currentCells) {
        int groundX = pos.x + static_cast<int>(gravityDir.x);
        int groundY = pos.y + static_cast<int>(gravityDir.y);

        // World boundary = treat as WALL (provides full support and friction).
        if (!data.inBounds(groundX, groundY)) {
            groundMaterials.push_back(Material::EnumType::Wall);
            continue;
        }

        const Cell& groundCell = data.at(groundX, groundY);

        // Skip empty cells and own cells.
        if (groundCell.isEmpty()) {
            continue;
        }
        Vector2i groundPos{ groundX, groundY };
        if (world.getOrganismManager().at(groundPos) == organismId) {
            continue;
        }

        groundMaterials.push_back(groundCell.material_type);
    }

    // No ground materials found = no friction.
    if (groundMaterials.empty()) {
        return { 0.0, 0.0 };
    }

    // Calculate average friction coefficient from all ground materials.
    double totalStaticFriction = 0.0;
    double totalKineticFriction = 0.0;
    double totalStickVelocity = 0.0;
    double totalTransitionWidth = 0.0;

    for (const auto& mat : groundMaterials) {
        const Material::Properties& props = Material::getProperties(mat);
        totalStaticFriction += props.static_friction_coefficient;
        totalKineticFriction += props.kinetic_friction_coefficient;
        totalStickVelocity += props.stick_velocity;
        totalTransitionWidth += props.friction_transition_width;
    }

    double count = static_cast<double>(groundMaterials.size());
    double staticFriction = totalStaticFriction / count;
    double kineticFriction = totalKineticFriction / count;
    double stickVelocity = totalStickVelocity / count;
    double transitionWidth = totalTransitionWidth / count;

    // Calculate friction coefficient with smooth static/kinetic transition.
    double frictionCoefficient = staticFriction;
    if (tangentialSpeed >= stickVelocity) {
        double t = (tangentialSpeed - stickVelocity) / transitionWidth;
        t = std::clamp(t, 0.0, 1.0);
        double smoothT = t * t * (3.0 - 2.0 * t); // Smooth cubic interpolation.
        frictionCoefficient = staticFriction * (1.0 - smoothT) + kineticFriction * smoothT;
    }

    // Calculate friction force magnitude.
    double frictionMagnitude = frictionCoefficient * normalForce;

    // Direction opposes tangential velocity.
    Vector2d frictionDirection =
        tangentialVelocity.x > 0.0 ? Vector2d{ -1.0, 0.0 } : Vector2d{ 1.0, 0.0 };

    return { frictionDirection.x * frictionMagnitude, 0.0 };
}

} // namespace DirtSim
