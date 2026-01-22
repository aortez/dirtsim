#include "WorldCollisionCalculator.h"
#include "Cell.h"
#include "GridOfCells.h"
#include "LoggingChannels.h"
#include "MaterialFragmentationParams.h"
#include "MaterialMove.h"
#include "MaterialType.h"
#include "PhysicsSettings.h"
#include "World.h"
#include "WorldCohesionCalculator.h"
#include "WorldData.h"
#include "WorldPressureCalculator.h"
#include "organisms/OrganismManager.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cmath>
#include <map>

using namespace DirtSim;

namespace {

// Helper: Check if material should have elastic collisions (METAL, WOOD, SEED, WALL).
bool isCollisionRigid(Material::EnumType type)
{
    return type == Material::EnumType::Metal || type == Material::EnumType::Wood
        || type == Material::EnumType::Seed || type == Material::EnumType::Wall;
}

} // namespace

// =================================================================
// COLLISION DETECTION.
// =================================================================

BoundaryCrossings WorldCollisionCalculator::getAllBoundaryCrossings(const Vector2d& newCOM) const
{
    BoundaryCrossings crossings;

    // Check each boundary independently (aligned with original shouldTransfer logic).
    if (newCOM.x >= 1.0) crossings.add(Vector2i(1, 0));   // Right boundary.
    if (newCOM.x <= -1.0) crossings.add(Vector2i(-1, 0)); // Left boundary.
    if (newCOM.y >= 1.0) crossings.add(Vector2i(0, 1));   // Down boundary.
    if (newCOM.y <= -1.0) crossings.add(Vector2i(0, -1)); // Up boundary.

    return crossings;
}

MaterialMove WorldCollisionCalculator::createCollisionAwareMove(
    const World& world,
    const Cell& fromCell,
    const Cell& toCell,
    const Vector2i& fromPos,
    const Vector2i& toPos,
    double /* deltaTime */) const
{
    MaterialMove move;

    // Standard move data.
    move.from = Vector2s(fromPos.x, fromPos.y);
    move.to = Vector2s(toPos.x, toPos.y);
    move.material = fromCell.material_type;

    // Calculate how much wants to transfer vs what can transfer.
    const double wants_to_transfer = fromCell.fill_ratio;
    const double capacity = toCell.getCapacity();

    // Queue only what will actually succeed.
    move.amount = std::min(wants_to_transfer, capacity);

    // Calculate excess that won't fit (for pressure generation).
    const double excess = wants_to_transfer - move.amount;
    move.pressure_from_excess = 0.0;

    if (excess > MIN_MATTER_THRESHOLD && world.getPhysicsSettings().pressure_dynamic_strength > 0) {
        const double blocked_mass = excess * Material::getDensity(fromCell.material_type);
        const double energy = fromCell.velocity.magnitude() * blocked_mass;
        const double dynamic_strength = world.getPhysicsSettings().pressure_dynamic_strength;
        const double pressure_increase =
            energy * 0.1 * dynamic_strength; // Apply dynamic pressure strength.

        // Store pressure to be applied to target cell when processing moves.
        move.pressure_from_excess = pressure_increase;

        spdlog::debug(
            "Pressure from excess at ({},{}) -> ({},{}): excess={:.3f}, energy={:.3f}, "
            "dynamic_strength={:.3f}, pressure_to_add={:.3f}",
            fromPos.x,
            fromPos.y,
            toPos.x,
            toPos.y,
            excess,
            energy,
            dynamic_strength,
            pressure_increase);
    }

    move.momentum = fromCell.velocity;

    // Calculate collision physics data.
    move.material_mass = calculateMaterialMass(fromCell);
    move.target_mass = calculateMaterialMass(toCell);
    move.collision_energy = calculateCollisionEnergy(move, fromCell, toCell);

    // Determine collision type based on materials and energy.
    move.collision_type =
        determineCollisionType(fromCell.material_type, toCell.material_type, move.collision_energy);

    // Single-cell organisms must not fragment via partial TRANSFER_ONLY.
    // Only allow full transfers into completely empty cells.
    OrganismId org_id = world.getOrganismManager().at(fromPos);
    if (org_id != INVALID_ORGANISM_ID && move.collision_type == CollisionType::TRANSFER_ONLY
        && !toCell.isEmpty()) {
        move.collision_type = CollisionType::ELASTIC_REFLECTION;
        spdlog::debug(
            "Organism at ({},{}) - target not empty (fill={:.2f}), forcing collision",
            fromPos.x,
            fromPos.y,
            toCell.fill_ratio);
    }

    // Set material-specific restitution coefficient.
    const auto& fromProps = fromCell.material();
    const auto& toProps = toCell.material();

    if (move.collision_type == CollisionType::ELASTIC_REFLECTION) {
        // For elastic collisions, use geometric mean of elasticities.
        move.restitution_coefficient = std::sqrt(fromProps.elasticity * toProps.elasticity);
    }
    else if (move.collision_type == CollisionType::INELASTIC_COLLISION) {
        // For inelastic collisions, reduce restitution significantly.
        move.restitution_coefficient = std::sqrt(fromProps.elasticity * toProps.elasticity) * 0.3;
    }
    else if (move.collision_type == CollisionType::FRAGMENTATION) {
        // Fragmentation has very low restitution.
        move.restitution_coefficient = 0.1;
    }
    else {
        // Transfer and absorption have minimal bounce.
        move.restitution_coefficient = 0.0;
    }

    return move;
}

CollisionType WorldCollisionCalculator::determineCollisionType(
    Material::EnumType from, Material::EnumType to, double collision_energy) const
{
    // Get material properties for both materials.
    const auto& fromProps = Material::getProperties(from);
    const auto& toProps = Material::getProperties(to);

    // Empty cells allow transfer.
    if (to == Material::EnumType::Air) {
        return CollisionType::TRANSFER_ONLY;
    }

    // High-energy impacts on brittle materials cause fragmentation.
    if (collision_energy > FRAGMENTATION_THRESHOLD
        && (from == Material::EnumType::Wood || from == Material::EnumType::Leaf)
        && (to == Material::EnumType::Metal || to == Material::EnumType::Wall)) {
        return CollisionType::FRAGMENTATION;
    }

    // Material-specific interaction matrix.

    // METAL interactions - highly elastic due to high elasticity (0.8)
    if (from == Material::EnumType::Metal || to == Material::EnumType::Metal) {
        if (to == Material::EnumType::Wall || from == Material::EnumType::Wall) {
            return CollisionType::ELASTIC_REFLECTION; // Metal vs wall.
        }
        if ((from == Material::EnumType::Metal && to == Material::EnumType::Metal)
            || (from == Material::EnumType::Metal && isCollisionRigid(to))
            || (to == Material::EnumType::Metal && isCollisionRigid(from))) {
            return CollisionType::ELASTIC_REFLECTION; // Metal vs rigid materials.
        }
        return CollisionType::INELASTIC_COLLISION; // Metal vs soft materials.
    }

    // WALL interactions - always elastic due to infinite mass.
    if (to == Material::EnumType::Wall) {
        return CollisionType::ELASTIC_REFLECTION;
    }

    // WOOD interactions - moderately elastic (0.6 elasticity)
    if (from == Material::EnumType::Wood && isCollisionRigid(to)) {
        return CollisionType::ELASTIC_REFLECTION;
    }

    // AIR interactions - highly elastic (1.0 elasticity) but low mass.
    if (from == Material::EnumType::Air) {
        return CollisionType::ELASTIC_REFLECTION;
    }

    // Rigid-to-rigid collisions based on elasticity.
    if (isCollisionRigid(from) && isCollisionRigid(to)) {
        const double avg_elasticity = (fromProps.elasticity + toProps.elasticity) / 2.0;
        return (avg_elasticity > 0.5) ? CollisionType::ELASTIC_REFLECTION
                                      : CollisionType::INELASTIC_COLLISION;
    }

    // Fluid absorption behaviors.
    if ((from == Material::EnumType::Water && to == Material::EnumType::Dirt)
        || (from == Material::EnumType::Dirt && to == Material::EnumType::Water)) {
        return CollisionType::ABSORPTION;
    }

    // Dense materials hitting lighter materials.
    if (fromProps.density > toProps.density * 2.0) {
        return CollisionType::INELASTIC_COLLISION; // Heavy impacts soft.
    }

    // Default: inelastic collision for general material interactions.
    return CollisionType::INELASTIC_COLLISION;
}

double WorldCollisionCalculator::calculateCollisionEnergy(
    const MaterialMove& move, const Cell& fromCell, const Cell& toCell) const
{
    // Kinetic energy: KE = 0.5 × m × v²
    // Use FULL cell mass for collision energy, not just transferable amount.
    // This is needed for swap decisions when target cell is full (move.amount = 0).
    const double movingMass = calculateMaterialMass(fromCell);

    // IMPORTANT: Use velocity component in direction of movement, not total magnitude.
    // For swaps, only energy in the swap direction matters.
    // If falling vertically with little horizontal velocity, horizontal swaps should be hard.
    const Vector2d direction_vector(move.to.x - move.from.x, move.to.y - move.from.y);
    const double velocity_in_direction = std::abs(move.momentum.dot(direction_vector));

    LOG_DEBUG(
        Swap,
        "Energy calc: total_vel=({:.3f},{:.3f}), dir=({},{}), vel_in_dir={:.3f}",
        move.momentum.x,
        move.momentum.y,
        move.to.x - move.from.x,
        move.to.y - move.from.y,
        velocity_in_direction);

    // If target cell has material, include reduced mass for collision.
    const double targetMass = calculateMaterialMass(toCell);
    double effective_mass = movingMass; // Modified when target has mass.

    if (targetMass > 0.0) {
        // Reduced mass formula: μ = (m1 × m2) / (m1 + m2)
        effective_mass = (movingMass * targetMass) / (movingMass + targetMass);
    }

    return 0.5 * effective_mass * velocity_in_direction * velocity_in_direction;
}

double WorldCollisionCalculator::calculateMaterialMass(const Cell& cell) const
{
    if (cell.isEmpty()) return 0.0;

    // Mass = density × volume.
    // Volume = fill_ratio (since cell volume is normalized to 1.0)
    const double density = Material::getDensity(cell.material_type);
    const double volume = cell.fill_ratio;
    return density * volume;
}

bool WorldCollisionCalculator::checkFloatingParticleCollision(
    const World& world, int cellX, int cellY, const Cell& floating_particle) const
{
    const auto& data = world.getData();
    if (!data.inBounds(cellX, cellY)) {
        return false;
    }

    const Cell& targetCell = data.at(cellX, cellY);

    // Check if there's material to collide with.
    if (!targetCell.isEmpty()) {
        // Get material properties for collision behavior.
        const Material::Properties& floatingProps =
            Material::getProperties(floating_particle.material_type);
        const Material::Properties& targetProps = Material::getProperties(targetCell.material_type);

        // For now, simple collision detection - can be enhanced later.
        // Heavy materials (like METAL) can push through lighter materials.
        // Solid materials (like WALL) stop everything.
        if (targetCell.material_type == Material::EnumType::Wall) {
            return true; // Wall stops everything.
        }

        // Check density-based collision.
        if (floatingProps.density <= targetProps.density) {
            return true; // Can't push through denser material.
        }
    }

    return false;
}

// =================================================================
// COLLISION RESPONSE.
// =================================================================

void WorldCollisionCalculator::handleTransferMove(
    World& world, Cell& fromCell, Cell& toCell, const MaterialMove& move)
{
    // Single-cell organisms must not fragment.
    // Re-check target is empty at execution time (moves are shuffled).
    const Vector2i from_pos{ move.from.x, move.from.y };
    const OrganismId org_id = world.getOrganismManager().at(from_pos);
    if (org_id != INVALID_ORGANISM_ID && !toCell.isEmpty()) {
        spdlog::info(
            "handleTransferMove: Organism at ({},{}) - target became non-empty (fill={:.2f}), "
            "aborting transfer",
            move.from.x,
            move.from.y,
            toCell.fill_ratio);
        // Apply bounce instead.
        const Vector2i direction(move.to.x - move.from.x, move.to.y - move.from.y);
        handleElasticCollision(fromCell, toCell, move);
        return;
    }

    // Log pre-transfer state.
    spdlog::debug(
        "TRANSFER: Before - From({},{}) vel=({:.3f},{:.3f}) fill={:.3f}, To({},{}) "
        "vel=({:.3f},{:.3f}) fill={:.3f}",
        move.from.x,
        move.from.y,
        fromCell.velocity.x,
        fromCell.velocity.y,
        fromCell.fill_ratio,
        move.to.x,
        move.to.y,
        toCell.velocity.x,
        toCell.velocity.y,
        toCell.fill_ratio);

    // Attempt the transfer.
    const double transferred =
        fromCell.transferToWithPhysics(toCell, move.amount, move.getDirection());

    // Log post-transfer state.
    spdlog::debug(
        "TRANSFER: After  - From({},{}) vel=({:.3f},{:.3f}) fill={:.3f}, To({},{}) "
        "vel=({:.3f},{:.3f}) fill={:.3f}",
        move.from.x,
        move.from.y,
        fromCell.velocity.x,
        fromCell.velocity.y,
        fromCell.fill_ratio,
        move.to.x,
        move.to.y,
        toCell.velocity.x,
        toCell.velocity.y,
        toCell.fill_ratio);

    if (transferred > 0.0) {
        spdlog::trace(
            "Transferred {:.3f} {} from ({},{}) to ({},{}) with boundary normal ({:.2f},{:.2f})",
            transferred,
            toString(move.material),
            move.from.x,
            move.from.y,
            move.to.x,
            move.to.y,
            move.getDirection().x,
            move.getDirection().y);
    }

    // Check if transfer was incomplete (target full or couldn't accept all material).
    const double transfer_deficit = move.amount - transferred;
    if (transfer_deficit > MIN_MATTER_THRESHOLD) {
        // Transfer failed partially or completely - apply elastic reflection for remaining
        // material.
        const Vector2i direction(move.to.x - move.from.x, move.to.y - move.from.y);

        spdlog::debug(
            "Transfer incomplete: requested={:.3f}, transferred={:.3f}, deficit={:.3f} - applying "
            "reflection",
            move.amount,
            transferred,
            transfer_deficit);

        // Queue blocked transfer for dynamic pressure accumulation.
        if (world.getPhysicsSettings().pressure_dynamic_strength > 0) {
            // Calculate energy with proper mass consideration.
            const double material_density = Material::getDensity(move.material);
            const double blocked_mass = transfer_deficit * material_density;
            const double energy = fromCell.velocity.magnitude() * blocked_mass;

            spdlog::debug(
                "Blocked transfer energy calculation: material={}, density={:.2f}, "
                "blocked_mass={:.4f}, velocity={:.2f}, energy={:.4f}",
                toString(move.material),
                material_density,
                blocked_mass,
                fromCell.velocity.magnitude(),
                energy);

            world.getPressureCalculator().queueBlockedTransfer(
                { move.from.x,
                  move.from.y,
                  move.to.x,
                  move.to.y,
                  static_cast<float>(transfer_deficit), // transfer_amount.
                  fromCell.velocity,
                  static_cast<float>(energy) });
        }

        applyCellBoundaryReflection(fromCell, direction, move.material);
    }
}

void WorldCollisionCalculator::handleElasticCollision(
    Cell& fromCell, Cell& toCell, const MaterialMove& move)
{
    const Vector2d incident_velocity = move.momentum;
    const Vector2d surface_normal = move.getDirection().normalize();

    if (move.target_mass > 0.0 && !toCell.isEmpty()) {
        // Two-body elastic collision with proper normal/tangential decomposition.
        const Vector2d target_velocity = toCell.velocity;
        const double m1 = move.material_mass;
        const double m2 = move.target_mass;

        // Decompose both velocities into normal and tangential components.
        const auto v1_comp = decomposeVelocity(incident_velocity, surface_normal);
        const auto v2_comp = decomposeVelocity(target_velocity, surface_normal);

        // Apply 1D elastic collision formulas ONLY to normal components.
        // v1_normal' = ((m1-m2)*v1_normal + 2*m2*v2_normal)/(m1+m2)
        // v2_normal' = ((m2-m1)*v2_normal + 2*m1*v1_normal)/(m1+m2)
        double v1_normal_new_scalar =
            ((m1 - m2) * v1_comp.normal_scalar + 2.0 * m2 * v2_comp.normal_scalar) / (m1 + m2);
        double v2_normal_new_scalar =
            ((m2 - m1) * v2_comp.normal_scalar + 2.0 * m1 * v1_comp.normal_scalar) / (m1 + m2);

        // Apply restitution coefficient ONLY to normal components.
        v1_normal_new_scalar *= move.restitution_coefficient;
        v2_normal_new_scalar *= move.restitution_coefficient;

        // Recombine: final velocity = tangential (preserved) + normal (modified).
        const Vector2d new_v1 = v1_comp.tangential + surface_normal * v1_normal_new_scalar;
        const Vector2d new_v2 = v2_comp.tangential + surface_normal * v2_normal_new_scalar;

        fromCell.velocity = new_v1;
        toCell.velocity = new_v2;

        // Separate particles to prevent repeated collisions.
        // Move the particle that crossed the boundary back slightly.
        constexpr double separation_distance = 0.02; // Small separation to ensure clean separation.
        Vector2d fromCOM = fromCell.com;             // Modified based on boundary crossing.

        // Check which boundary was crossed and apply separation.
        if (move.getDirection().x > 0.5) { // Crossed right boundary (normal points left)
            fromCOM.x = std::min(fromCOM.x, 1.0 - separation_distance);
            fromCell.setCOM(fromCOM);
        }
        else if (move.getDirection().x < -0.5) { // Crossed left boundary (normal points right)
            fromCOM.x = std::max(fromCOM.x, -1.0 + separation_distance);
            fromCell.setCOM(fromCOM);
        }

        if (move.getDirection().y > 0.5) { // Crossed bottom boundary (normal points up)
            fromCOM.y = std::min(fromCOM.y, 1.0 - separation_distance);
            fromCell.setCOM(fromCOM);
        }
        else if (move.getDirection().y < -0.5) { // Crossed top boundary (normal points down)
            fromCOM.y = std::max(fromCOM.y, -1.0 + separation_distance);
            fromCell.setCOM(fromCOM);
        }

        spdlog::trace(
            "Elastic collision: {} vs {} at ({},{}) -> ({},{}) - masses: {:.2f}, {:.2f}, "
            "restitution: {:.2f}, COM adjusted to ({:.3f},{:.3f})",
            toString(move.material),
            toString(toCell.material_type),
            move.from.x,
            move.from.y,
            move.to.x,
            move.to.y,
            m1,
            m2,
            move.restitution_coefficient,
            fromCOM.x,
            fromCOM.y);
    }
    else {
        // Empty target or zero mass - reflect off surface with proper decomposition.
        const auto v_comp = decomposeVelocity(incident_velocity, surface_normal);

        // Apply restitution only to normal component, preserve tangential.
        const Vector2d v_normal_reflected = v_comp.normal * (-move.restitution_coefficient);
        const Vector2d reflected_velocity = v_comp.tangential + v_normal_reflected;

        fromCell.velocity = reflected_velocity;

        // Also apply separation for reflections.
        constexpr double separation_distance = 0.02;
        Vector2d fromCOM = fromCell.com; // Modified based on boundary crossing.

        if (surface_normal.x > 0.5) {
            fromCOM.x = std::min(fromCOM.x, 1.0 - separation_distance);
        }
        else if (surface_normal.x < -0.5) {
            fromCOM.x = std::max(fromCOM.x, -1.0 + separation_distance);
        }

        if (surface_normal.y > 0.5) {
            fromCOM.y = std::min(fromCOM.y, 1.0 - separation_distance);
        }
        else if (surface_normal.y < -0.5) {
            fromCOM.y = std::max(fromCOM.y, -1.0 + separation_distance);
        }

        fromCell.setCOM(fromCOM);
    }

    // Minimal or no material transfer for elastic collisions.
    // Material stays in original cell with new velocity.
}

void WorldCollisionCalculator::handleInelasticCollision(
    World& world, Cell& fromCell, Cell& toCell, const MaterialMove& move)
{
    // Physics-correct component-based collision handling.
    const Vector2d incident_velocity = move.momentum;
    const Vector2d surface_normal = move.getDirection().normalize();

    // Decompose velocity into normal and tangential components.
    const auto v_comp = decomposeVelocity(incident_velocity, surface_normal);

    // Apply restitution only to normal component, preserve tangential.
    const double inelastic_restitution =
        move.restitution_coefficient * INELASTIC_RESTITUTION_FACTOR;
    const Vector2d v_normal_reflected = v_comp.normal * (-inelastic_restitution);
    const Vector2d final_velocity = v_comp.tangential + v_normal_reflected;

    // Apply the corrected velocity to the incident particle.
    fromCell.velocity = final_velocity;

    // Transfer momentum to target cell (Newton's 3rd law).
    // Even if material transfer fails, momentum must be conserved.
    if (move.target_mass > 0.0) {
        const Vector2d momentum_transferred =
            v_comp.normal * (1.0 + inelastic_restitution) * move.material_mass;
        const Vector2d target_velocity_change = momentum_transferred / move.target_mass;
        toCell.velocity = toCell.velocity + target_velocity_change;

        spdlog::debug(
            "Momentum transfer: normal=({:.3f},{:.3f}) momentum=({:.3f},{:.3f}) "
            "target_vel_change=({:.3f},{:.3f})",
            v_comp.normal.x,
            v_comp.normal.y,
            momentum_transferred.x,
            momentum_transferred.y,
            target_velocity_change.x,
            target_velocity_change.y);
    }

    // Allow material transfer based on natural capacity limits.
    const double transfer_amount = move.amount; // Full amount, let capacity decide.

    // Attempt direct material transfer and measure actual amount transferred.
    const double actual_transfer =
        fromCell.transferToWithPhysics(toCell, transfer_amount, move.getDirection());

    // Check for blocked transfer and queue for dynamic pressure accumulation.
    const double transfer_deficit = transfer_amount - actual_transfer;

    if (transfer_deficit > MIN_MATTER_THRESHOLD
        && world.getPhysicsSettings().pressure_dynamic_strength > 0) {

        // Queue blocked transfer for dynamic pressure accumulation.
        // Calculate energy with proper mass consideration.
        const double material_density = Material::getDensity(move.material);
        const double blocked_mass = transfer_deficit * material_density;
        const double energy = fromCell.velocity.magnitude() * blocked_mass;

        spdlog::debug(
            "Inelastic collision blocked energy: material={}, density={:.2f}, "
            "blocked_mass={:.4f}, velocity={:.2f}, energy={:.4f}",
            toString(move.material),
            material_density,
            blocked_mass,
            fromCell.velocity.magnitude(),
            energy);

        world.getPressureCalculator().queueBlockedTransfer(
            { move.from.x,
              move.from.y,
              move.to.x,
              move.to.y,
              static_cast<float>(transfer_deficit),
              fromCell.velocity,
              static_cast<float>(energy) });
    }
}

void WorldCollisionCalculator::handleFragmentation(
    World& world, Cell& fromCell, Cell& toCell, const MaterialMove& move)
{
    // TODO: Implement fragmentation mechanics.
    // For now, treat as inelastic collision with complete material transfer.
    spdlog::debug(
        "Fragmentation collision: {} at ({},{}) - treating as inelastic for now",
        toString(move.material),
        move.from.x,
        move.from.y);

    handleInelasticCollision(world, fromCell, toCell, move);
}

void WorldCollisionCalculator::handleAbsorption(
    World& world, Cell& fromCell, Cell& toCell, const MaterialMove& move)
{
    // One material absorbs the other - implement absorption logic.
    if (move.material == Material::EnumType::Water
        && toCell.material_type == Material::EnumType::Dirt) {
        // Water absorbed by dirt - transfer all water.
        handleTransferMove(world, fromCell, toCell, move);
        spdlog::trace("Absorption: WATER absorbed by DIRT at ({},{})", move.to.x, move.to.y);
    }
    else if (
        move.material == Material::EnumType::Dirt
        && toCell.material_type == Material::EnumType::Water) {
        // Dirt falls into water - mix materials.
        handleTransferMove(world, fromCell, toCell, move);
        spdlog::trace("Absorption: DIRT mixed with WATER at ({},{})", move.to.x, move.to.y);
    }
    else {
        // Default to regular transfer.
        handleTransferMove(world, fromCell, toCell, move);
    }
}

// Helper struct for fragment placement.
struct FragTarget {
    Vector2i offset;
    Vector2d velocity;
    double amount;
};

// Helper function to generate and place fragments from a single cell.
// Returns the total amount of material that was successfully sprayed out.
double WorldCollisionCalculator::fragmentSingleCell(
    World& world,
    Cell& sourceCell,
    int sourceX,
    int sourceY,
    int avoidX,
    int avoidY,
    const Vector2d& spray_direction,
    int num_frags,
    double arc_width,
    const FragmentationParams& frag_params)
{
    if (num_frags < 2) {
        return 0.0;
    }
    if (arc_width <= 0.0) {
        return 0.0;
    }
    if (sourceCell.fill_ratio < World::MIN_MATTER_THRESHOLD) {
        return 0.0;
    }
    if (frag_params.spray_fraction <= 0.0) {
        return 0.0;
    }

    // Calculate frag angles spread evenly across the arc, centered on spray direction.
    // Fragments are distributed from -half_arc to +half_arc.
    std::vector<double> frag_angles;
    const double half_arc = arc_width / 2.0;
    if (num_frags == 2) {
        // Two fragments at the edges of the arc.
        frag_angles = { -half_arc, half_arc };
    }
    else {
        // 3+ fragments: distribute evenly across the arc.
        // E.g., 3 frags: -half, 0, +half
        //       4 frags: -half, -half/3, +half/3, +half
        //       5 frags: -half, -half/2, 0, +half/2, +half
        for (int i = 0; i < num_frags; i++) {
            const double t = static_cast<double>(i) / (num_frags - 1); // 0.0 to 1.0
            const double angle = -half_arc + t * arc_width;
            frag_angles.push_back(angle);
        }
    }

    // Calculate base angle of spray direction.
    const double base_angle = std::atan2(spray_direction.y, spray_direction.x);

    std::vector<FragTarget> frag_targets;
    const double frag_amount_each =
        (sourceCell.fill_ratio * frag_params.spray_fraction) / num_frags;
    if (frag_amount_each <= 0.0) {
        return 0.0;
    }

    for (const double angle_offset : frag_angles) {
        const double frag_angle = base_angle + angle_offset;

        // Convert angle to unit vector.
        const Vector2d frag_dir(std::cos(frag_angle), std::sin(frag_angle));

        // Calculate speed: edge fragments are faster to avoid self-collision.
        // Speed scales from 1.0 at center to edge_speed_factor at edges.
        const double edge_factor =
            std::abs(angle_offset) / half_arc; // 0.0 at center, 1.0 at edges.
        const double speed_multiplier = 1.0 + (frag_params.edge_speed_factor - 1.0) * edge_factor;
        const double frag_speed = frag_params.base_speed * speed_multiplier;

        // Map to nearest of 8 neighbor directions.
        // Neighbors are at angles: 0, 45, 90, 135, 180, 225, 270, 315 degrees.
        int best_dx = 0, best_dy = 0;
        double best_dot = -2.0;

        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;

                Vector2d neighbor_dir(dx, dy);
                neighbor_dir = neighbor_dir.normalize();
                double dot = frag_dir.dot(neighbor_dir);

                if (dot > best_dot) {
                    best_dot = dot;
                    best_dx = dx;
                    best_dy = dy;
                }
            }
        }

        const Vector2i offset(best_dx, best_dy);
        const Vector2d velocity = frag_dir * frag_speed;

        frag_targets.push_back({ offset, velocity, frag_amount_each });
    }

    // Merge fragments going to the same cell.
    std::map<std::pair<int, int>, FragTarget> merged_targets;
    for (const auto& frag : frag_targets) {
        auto key = std::make_pair(frag.offset.x, frag.offset.y);
        auto it = merged_targets.find(key);
        if (it != merged_targets.end()) {
            // Average velocities, sum amounts.
            double total_amount = it->second.amount + frag.amount;
            if (total_amount <= 0.0) {
                continue;
            }
            it->second.velocity =
                (it->second.velocity * it->second.amount + frag.velocity * frag.amount)
                / total_amount;
            it->second.amount = total_amount;
        }
        else {
            merged_targets[key] = frag;
        }
    }

    // Try to place fragments in destination cells.
    const WorldData& data = world.getData();
    double total_sprayed = 0.0;

    for (auto& [key, frag] : merged_targets) {
        const int target_x = sourceX + frag.offset.x;
        const int target_y = sourceY + frag.offset.y;

        // Skip if out of bounds.
        if (target_x < 0 || target_x >= data.width || target_y < 0 || target_y >= data.height) {
            continue;
        }

        // Skip if this is the cell we're avoiding (the collision partner).
        if (target_x == avoidX && target_y == avoidY) {
            continue;
        }

        Cell& target = world.getData().at(target_x, target_y);

        // Check capacity.
        const double capacity = target.getCapacity();
        if (capacity < MIN_MATTER_THRESHOLD) {
            continue; // No room.
        }

        // Transfer what fits.
        double to_transfer = std::min(frag.amount, capacity);
        to_transfer = std::min(to_transfer, sourceCell.fill_ratio - MIN_MATTER_THRESHOLD);

        constexpr double MIN_VISIBLE_FRAGMENT = 0.01;
        if (to_transfer < MIN_VISIBLE_FRAGMENT) {
            continue;
        }

        // Place the fragment at the edge of the destination cell, facing inward.
        // COM should be at the edge nearest the source cell.
        const Vector2d landing_com(-frag.offset.x * 0.9, -frag.offset.y * 0.9);

        // Add material to target cell.
        if (target.isEmpty()) {
            target.material_type = Material::EnumType::Water;
            target.fill_ratio = to_transfer;
            target.setCOM(landing_com);
            target.velocity = frag.velocity;
        }
        else if (target.material_type == Material::EnumType::Water) {
            // Merge with existing water.
            const double old_mass = target.fill_ratio;
            const double new_mass = to_transfer;
            const double total_mass = old_mass + new_mass;

            target.velocity = (target.velocity * old_mass + frag.velocity * new_mass) / total_mass;
            target.setCOM((target.com * old_mass + landing_com * new_mass) / total_mass);
            target.fill_ratio += to_transfer;
        }
        else {
            // Different material - skip this target.
            continue;
        }

        // Remove from source.
        sourceCell.fill_ratio -= to_transfer;
        total_sprayed += to_transfer;
    }

    if (sourceCell.fill_ratio < World::MIN_MATTER_THRESHOLD) {
        sourceCell = Cell();
    }

    return total_sprayed;
}

bool WorldCollisionCalculator::handleWaterFragmentation(
    World& world, Cell& fromCell, Cell& toCell, const MaterialMove& move, std::mt19937& rng)
{
    const PhysicsSettings& settings = world.getPhysicsSettings();

    // Check if fragmentation is enabled.
    if (!settings.fragmentation_enabled) {
        return false;
    }

    // Check energy threshold.
    if (move.collision_energy < settings.fragmentation_threshold) {
        return false;
    }

    // At least one cell must be water to fragment.
    const bool from_is_water = fromCell.material_type == Material::EnumType::Water;
    const bool to_is_water = toCell.material_type == Material::EnumType::Water;

    if (!from_is_water && !to_is_water) {
        return false;
    }

    // Calculate fragmentation probability: linear ramp from threshold to full_threshold.
    double probability = (move.collision_energy - settings.fragmentation_threshold)
        / (settings.fragmentation_full_threshold - settings.fragmentation_threshold);
    probability = std::clamp(probability, 0.0, 1.0);

    // Roll dice.
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    if (prob_dist(rng) > probability) {
        return false; // No fragmentation this time.
    }

    // Determine number of fragments (2-5) based on energy.
    // Higher energy = more fragments to fill the arc.
    int num_frags = 1; // Modified based on energy level.
    const double energy = move.collision_energy;
    const double full = settings.fragmentation_full_threshold;
    if (energy > full * 2.0) {
        num_frags = 5; // Extreme energy: full hemisphere coverage.
    }
    else if (energy > full * 1.5) {
        num_frags = 4;
    }
    else if (energy > full) {
        num_frags = 3;
    }
    else if (energy > settings.fragmentation_threshold) {
        num_frags = 2;
    }

    // 1 frag means normal collision behavior, no fragmentation.
    if (num_frags == 1) {
        return false;
    }

    // Get fragmentation params for each cell's material type.
    const FragmentationParams from_params = getMaterialFragmentationParams(fromCell.material_type);
    const FragmentationParams to_params = getMaterialFragmentationParams(toCell.material_type);

    // =================================================================
    // Calculate spray directions for both cells.
    // Blend between radial (explosion-like) and reflection (momentum-preserving).
    // =================================================================

    const Vector2d surface_normal = move.getDirection().normalize();

    // Radial directions: simply away from collision partner.
    const Vector2d from_radial_dir = surface_normal * -1.0; // FROM sprays away from TO.
    const Vector2d to_radial_dir = surface_normal;          // TO sprays away from FROM.

    // Reflection direction for FROM cell (using FROM's momentum).
    const auto from_v_comp = decomposeVelocity(move.momentum, surface_normal);
    Vector2d from_reflect_dir =
        (from_v_comp.tangential - from_v_comp.normal).normalize(); // May be replaced.
    if (from_reflect_dir.magnitude() < 0.01) {
        from_reflect_dir = from_radial_dir;
    }

    // Reflection direction for TO cell (using TO's own velocity for correctness).
    const auto to_v_comp = decomposeVelocity(toCell.velocity, surface_normal);
    Vector2d to_reflect_dir =
        (to_v_comp.tangential + to_v_comp.normal).normalize(); // May be replaced.
    if (to_reflect_dir.magnitude() < 0.01) {
        to_reflect_dir = to_radial_dir;
    }

    // Blend radial and reflection directions using each cell's params.
    Vector2d from_spray_dir = (from_radial_dir * from_params.radial_bias
                               + from_reflect_dir * (1.0 - from_params.radial_bias))
                                  .normalize();
    Vector2d to_spray_dir =
        (to_radial_dir * to_params.radial_bias + to_reflect_dir * (1.0 - to_params.radial_bias))
            .normalize();

    // Fallback if blending produces zero vector.
    if (from_spray_dir.magnitude() < 0.01) {
        from_spray_dir = from_radial_dir;
    }
    if (to_spray_dir.magnitude() < 0.01) {
        to_spray_dir = to_radial_dir;
    }

    // =================================================================
    // Calculate arc width based on collision energy for each cell.
    // Scales from min_arc at threshold to max_arc at high energy.
    // =================================================================

    double energy_ratio = (move.collision_energy - settings.fragmentation_threshold)
        / (settings.fragmentation_full_threshold - settings.fragmentation_threshold);
    energy_ratio = std::clamp(energy_ratio, 0.0, 2.0); // Allow overshoot for very high energy.

    double from_arc_width =
        from_params.min_arc + (from_params.max_arc - from_params.min_arc) * energy_ratio;
    from_arc_width = std::min(from_arc_width, from_params.max_arc);

    double to_arc_width =
        to_params.min_arc + (to_params.max_arc - to_params.min_arc) * energy_ratio;
    to_arc_width = std::min(to_arc_width, to_params.max_arc);

    // Compute base speeds from collision momentum.
    const double momentum_magnitude = move.momentum.magnitude();

    // Fragment FROM cell if it's water.
    double from_sprayed = 0.0;
    if (from_is_water) {
        FragmentationParams params = from_params;
        params.base_speed = momentum_magnitude * from_params.base_speed;
        from_sprayed = fragmentSingleCell(
            world,
            fromCell,
            move.from.x,
            move.from.y,
            move.to.x,
            move.to.y,
            from_spray_dir,
            num_frags,
            from_arc_width,
            params);
    }

    // Fragment TO cell if it's water (mutual fragmentation!).
    double to_sprayed = 0.0;
    if (to_is_water) {
        FragmentationParams params = to_params;
        params.base_speed = momentum_magnitude * to_params.base_speed;
        to_sprayed = fragmentSingleCell(
            world,
            toCell,
            move.to.x,
            move.to.y,
            move.from.x,
            move.from.y,
            to_spray_dir,
            num_frags,
            to_arc_width,
            params);
    }

    // If nothing sprayed from either cell, fragmentation failed.
    if (from_sprayed < MIN_MATTER_THRESHOLD && to_sprayed < MIN_MATTER_THRESHOLD) {
        return false;
    }

    // Handle remaining material in both cells with inelastic reflection.
    const double inelastic_restitution =
        move.restitution_coefficient * INELASTIC_RESTITUTION_FACTOR;

    if (from_is_water && fromCell.fill_ratio > MIN_MATTER_THRESHOLD) {
        const Vector2d v_normal_reflected = from_v_comp.normal * (-inelastic_restitution);
        fromCell.velocity = from_v_comp.tangential + v_normal_reflected;
    }
    else if (from_is_water) {
        fromCell.clear();
    }

    // Transfer momentum between cells.
    if (move.target_mass > 0.0 && !toCell.isEmpty() && from_is_water) {
        const Vector2d momentum_transferred =
            from_v_comp.normal * (1.0 + inelastic_restitution) * move.material_mass;
        const Vector2d target_velocity_change = momentum_transferred / move.target_mass;
        toCell.velocity = toCell.velocity + target_velocity_change;
    }

    spdlog::debug(
        "Water fragmentation: {} frags, FROM({},{}) sprayed {:.3f} remaining {:.3f}, TO({},{}) "
        "sprayed {:.3f} remaining {:.3f}",
        num_frags,
        move.from.x,
        move.from.y,
        from_sprayed,
        fromCell.fill_ratio,
        move.to.x,
        move.to.y,
        to_sprayed,
        toCell.fill_ratio);

    return true;
}

void WorldCollisionCalculator::applyBoundaryReflection(Cell& cell, const Vector2i& direction)
{
    Vector2d velocity = cell.velocity; // Modified based on direction.
    Vector2d com = cell.com;           // Modified based on direction.
    const double elasticity = Material::getProperties(cell.material_type).elasticity;

    spdlog::debug(
        "Applying boundary reflection: material={} direction=({},{}) elasticity={:.2f} "
        "velocity=({:.2f},{:.2f})",
        toString(cell.material_type),
        direction.x,
        direction.y,
        elasticity,
        velocity.x,
        velocity.y);

    // Apply elastic reflection for the component perpendicular to the boundary.
    if (direction.x != 0) { // Horizontal boundary (left/right walls)
        velocity.x = -velocity.x * elasticity;
        // Move COM away from boundary to prevent re-triggering boundary detection.
        com.x = (direction.x > 0) ? 0.99 : -0.99;
    }

    if (direction.y != 0) { // Vertical boundary (top/bottom walls)
        velocity.y = -velocity.y * elasticity;
        // Move COM away from boundary to prevent re-triggering boundary detection.
        com.y = (direction.y > 0) ? 0.99 : -0.99;
    }

    cell.velocity = velocity;
    cell.setCOM(com);

    spdlog::debug(
        "Boundary reflection complete: new_velocity=({:.2f},{:.2f}) new_com=({:.2f},{:.2f})",
        velocity.x,
        velocity.y,
        com.x,
        com.y);
}

void WorldCollisionCalculator::applyCellBoundaryReflection(
    Cell& cell, const Vector2i& direction, Material::EnumType material)
{
    Vector2d velocity = cell.velocity; // Modified based on direction.
    Vector2d com = cell.com;           // Modified based on direction.
    const double elasticity = Material::getProperties(material).elasticity;

    spdlog::debug(
        "Applying cell boundary reflection: material={} direction=({},{}) elasticity={:.2f}",
        toString(material),
        direction.x,
        direction.y,
        elasticity);

    // Apply elastic reflection when transfer between cells fails.
    if (direction.x != 0) { // Horizontal transfer failed.
        velocity.x = -velocity.x * elasticity;
        // Move COM away from the boundary that caused the failed transfer.
        com.x = (direction.x > 0) ? 0.99 : -0.99;
    }

    if (direction.y != 0) { // Vertical transfer failed.
        velocity.y = -velocity.y * elasticity;
        // Move COM away from the boundary that caused the failed transfer.
        com.y = (direction.y > 0) ? 0.99 : -0.99;
    }

    cell.velocity = velocity;
    cell.setCOM(com);

    spdlog::debug(
        "Cell boundary reflection complete: new_velocity=({:.2f},{:.2f}) new_com=({:.2f},{:.2f})",
        velocity.x,
        velocity.y,
        com.x,
        com.y);
}

bool WorldCollisionCalculator::densitySupportsSwap(
    const Cell& fromCell, const Cell& toCell, const Vector2i& direction) const
{
    const double from_density = Material::getProperties(fromCell.material_type).density;
    const double to_density = Material::getProperties(toCell.material_type).density;

    if (direction.y > 0) {
        // Moving downward: heavier material should sink.
        return from_density > to_density;
    }
    else {
        // Moving upward: lighter material should rise.
        return from_density < to_density;
    }
}

bool WorldCollisionCalculator::shouldSwapMaterials(
    const World& world,
    int fromX,
    int fromY,
    const Cell& fromCell,
    const Cell& toCell,
    const Vector2i& direction,
    const MaterialMove& move) const
{
    if (fromCell.material_type == toCell.material_type) {
        LOG_DEBUG(Swap, "Swap denied: same material type");
        return false;
    }

    // Rigid body organisms resist displacement (they manage their own physics).
    // Single-cell organisms (like Duck) participate in normal cell physics including swaps.
    Vector2i toPos{ fromX + direction.x, fromY + direction.y };
    OrganismId toOrgId = world.getOrganismManager().at(toPos);
    if (toOrgId != INVALID_ORGANISM_ID) {
        const Organism::Body* organism = world.getOrganismManager().getOrganism(toOrgId);
        if (organism && organism->usesRigidBodyPhysics()) {
            LOG_DEBUG(
                Swap,
                "Swap denied: cannot displace rigid body organism cell {} (organism_id={})",
                toString(toCell.material_type),
                toOrgId);
            return false;
        }
    }

    const Material::Properties& to_props = Material::getProperties(toCell.material_type);

    // PATH OF LEAST RESISTANCE CHECK.
    // When a vertical swap would displace a fluid (but not AIR), check if that
    // fluid has easier lateral escape routes. If so, deny the swap and let
    // pressure push the fluid sideways instead. This prevents the "cliff climbing"
    // effect where dirt drops through water, pushing water up through solid.
    // AIR is excluded because we want air pockets to fill in naturally.
    const Material::Properties& from_props = Material::getProperties(fromCell.material_type);
    if (direction.y != 0 && to_props.is_fluid && toCell.material_type != Material::EnumType::Air) {
        const WorldData& data = world.getData();
        const int toX = fromX + direction.x;
        const int toY = fromY + direction.y;

        for (const int dx : { -1, 1 }) {
            const int nx = toX + dx;
            if (!data.inBounds(nx, toY)) {
                continue;
            }

            const Cell& lateral = data.at(nx, toY);

            // If the fluid being displaced has empty space beside it, deny swap.
            // The fluid should escape sideways via pressure, not be pushed vertically.
            if (lateral.isEmpty()) {
                LOG_INFO(
                    Swap,
                    "Swap denied (path of least resistance): "
                    "{} at ({},{}) can escape to empty lateral at ({},{})",
                    toString(toCell.material_type),
                    toX,
                    toY,
                    nx,
                    toY);
                return false;
            }

            // Lower pressure laterally means easier escape for the displaced fluid.
            const double lateral_pressure = lateral.pressure;
            const double target_pressure = toCell.pressure;
            if (lateral_pressure < target_pressure * 0.5) {
                LOG_INFO(
                    Swap,
                    "Swap denied (path of least resistance): "
                    "{} at ({},{}) can escape to lower pressure ({:.2f} vs {:.2f}) at ({},{})",
                    toString(toCell.material_type),
                    toX,
                    toY,
                    lateral_pressure,
                    target_pressure,
                    nx,
                    toY);
                return false;
            }
        }
    }

    // Check swap requirements based on direction.
    const PhysicsSettings& settings = world.getPhysicsSettings();
    if (direction.y == 0) {
        // Horizontal swap: momentum-based displacement.
        // FROM cell needs enough momentum to push TO cell out of the way.
        const double from_mass = from_props.density * fromCell.fill_ratio;
        const double from_velocity = std::abs(fromCell.velocity.x);
        double from_momentum = from_mass * from_velocity; // Modified for fluids pushing solids.

        // Fluids pushing solids sideways is harder - they flow around instead.
        if (from_props.is_fluid && !to_props.is_fluid) {
            from_momentum *= settings.horizontal_non_fluid_penalty;
        }

        // TO: resistance to being displaced.
        const double to_mass = to_props.density * toCell.fill_ratio;

        // Use cached neighbor-based cohesion (computed during applyCohesionForces).
        const int toX = fromX + direction.x;
        const int toY = fromY + direction.y;
        const double cohesion_strength = world.getGrid().getCohesionResistance(toX, toY);

        // Opposing momentum: target velocity against swap direction increases resistance.
        const Vector2f dir_vec(direction.x, direction.y);
        const double opposing_momentum = std::max(0.0f, -toCell.velocity.dot(dir_vec)) * to_mass;

        // Fluids are easier to displace than solids.
        const double fluid_factor = 1; // to_props.is_fluid ? 0.2 : 1.0;

        double to_resistance = (to_mass + cohesion_strength + opposing_momentum)
            * fluid_factor; // Modified by COM factor.

        // COM distance factor: swaps are harder when COMs are far from shared boundary.
        double com_distance = 0.0;
        if (direction.x > 0) {
            // Moving right: from exits at x=+1, to is entered at x=-1.
            double from_dist = 1.0 - fromCell.com.x;    // 0 at boundary, 2 at far edge.
            double to_dist = toCell.com.x + 1.0;        // 0 at boundary, 2 at far edge.
            com_distance = (from_dist + to_dist) / 4.0; // Normalize to 0-1.
        }
        else {
            // Moving left: from exits at x=-1, to is entered at x=+1.
            double from_dist = fromCell.com.x + 1.0;    // 0 at boundary, 2 at far edge.
            double to_dist = 1.0 - toCell.com.x;        // 0 at boundary, 2 at far edge.
            com_distance = (from_dist + to_dist) / 4.0; // Normalize to 0-1.
        }

        // Apply resistance multiplier: 1x at distance <= 0.3, scaling up at higher distances.
        // Threshold lowered to 0.3 so displaced material (COM at far edge) blocks cascades.
        const double com_resistance_multiplier =
            com_distance > 0.3 ? 1.0 + (com_distance - 0.3) * 14.0 : 1.0; // 1x at 0.3, ~11x at 1.0
        to_resistance *= com_resistance_multiplier;

        // Swap if momentum overcomes resistance.
        const double threshold = settings.horizontal_flow_resistance_factor;
        const bool swap_ok = from_momentum > to_resistance * threshold;

        if (!swap_ok) {
            return false;
        }
        // Log horizontal swap approval details.
        if (toCell.material_type != Material::EnumType::Air) {
            // LOG_WARN(Swap,
            //     "Horizontal swap OK: {} -> {} at ({},{}) -> ({},{}) | momentum: {:.3f} (mass: "
            //     "{:.3f}, "
            //     "vel: {:.3f}) | resistance: {:.3f} (mass: {:.3f}, cohesion: {:.3f}, support: "
            //     "{:.1f}, "
            //     "fluid: {:.1f}) | threshold: {:.3f}",
            //     toString(fromCell.material_type),
            //     toString(toCell.material_type),
            //     fromX,
            //     fromY,
            //     fromX + direction.x,
            //     fromY + direction.y,
            //     from_momentum,
            //     from_mass,
            //     from_velocity,
            //     to_resistance,
            //     to_mass,
            //     to_props.cohesion,
            //     support_factor,
            //     fluid_factor,
            //     to_resistance * threshold);
        }
    }
    else {
        // Vertical swap: momentum-based with buoyancy assist.
        // Density must support the swap direction AND momentum must overcome resistance.
        const double from_density = from_props.density;
        const double to_density = to_props.density;
        const bool density_ok = densitySupportsSwap(fromCell, toCell, direction);

        if (!density_ok) {
            return false;
        }

        // FROM: momentum in direction of movement.
        const double from_mass = from_props.density * fromCell.fill_ratio;
        const double from_velocity = std::abs(fromCell.velocity.y);
        const double from_momentum = from_mass * from_velocity;

        // Buoyancy adds "free" momentum based on density difference.
        // Larger density differences create stronger buoyancy forces.
        const double density_diff = std::abs(from_density - to_density);
        const double buoyancy_boost =
            density_diff * world.getPhysicsSettings().buoyancy_energy_scale;
        const double effective_momentum = from_momentum + buoyancy_boost;

        // TO: resistance to being displaced.
        // For vertical swaps, no fluid_factor - must move the mass regardless of fluidity.
        const double to_mass = to_props.density * toCell.fill_ratio;

        // Use cached neighbor-based cohesion (computed during applyCohesionForces).
        const int toX = fromX + direction.x;
        const int toY = fromY + direction.y;
        const double cohesion_strength = world.getGrid().getCohesionResistance(toX, toY);

        // Opposing momentum: target velocity against swap direction increases resistance.
        const Vector2f dir_vec(direction.x, direction.y);
        const double opposing_momentum = std::max(0.0f, -toCell.velocity.dot(dir_vec)) * to_mass;

        double to_resistance =
            to_mass + cohesion_strength + opposing_momentum; // Modified by COM factor.

        // COM distance factor: swaps are harder when COMs are far from shared boundary.
        // This prevents cascading swaps where freshly-displaced material hasn't
        // "arrived" at the boundary yet.
        double com_distance = 0.0;
        if (direction.y > 0) {
            // Moving down: from exits at y=+1, to is entered at y=-1.
            double from_dist = 1.0 - fromCell.com.y;    // 0 at boundary, 2 at far edge.
            double to_dist = toCell.com.y + 1.0;        // 0 at boundary, 2 at far edge.
            com_distance = (from_dist + to_dist) / 4.0; // Normalize to 0-1.
        }
        else {
            // Moving up: from exits at y=-1, to is entered at y=+1.
            double from_dist = fromCell.com.y + 1.0;    // 0 at boundary, 2 at far edge.
            double to_dist = 1.0 - toCell.com.y;        // 0 at boundary, 2 at far edge.
            com_distance = (from_dist + to_dist) / 4.0; // Normalize to 0-1.
        }

        // Apply resistance multiplier: 1x at distance <= 0.3, scaling up at higher distances.
        // Threshold lowered to 0.3 so displaced material (COM at far edge) blocks cascades.
        const double com_resistance_multiplier =
            com_distance > 0.3 ? 1.0 + (com_distance - 0.3) * 14.0 : 1.0; // 1x at 0.3, ~11x at 1.0
        to_resistance *= com_resistance_multiplier;

        // Swap if effective momentum overcomes resistance.
        const double threshold = world.getPhysicsSettings().horizontal_flow_resistance_factor;
        const bool swap_ok = effective_momentum > to_resistance * threshold;

        if (!swap_ok) {
            LOG_INFO(
                Swap,
                "Vertical swap DENIED: {} -> {} at ({},{}) -> ({},{}) | momentum: {:.3f} (mass: "
                "{:.3f}, vel: {:.3f}, buoyancy: {:.3f}) | resistance: {:.3f} (mass: {:.3f}, "
                "cohesion: {:.3f}, opposing: {:.3f}, com_dist: {:.2f}, com_mult: {:.1f}) | "
                "threshold: {:.3f} | dir.y: {} ({})",
                toString(fromCell.material_type),
                toString(toCell.material_type),
                fromX,
                fromY,
                toX,
                toY,
                effective_momentum,
                from_mass,
                from_velocity,
                buoyancy_boost,
                to_resistance,
                to_mass,
                cohesion_strength,
                opposing_momentum,
                com_distance,
                com_resistance_multiplier,
                to_resistance * threshold,
                direction.y,
                direction.y > 0 ? "DOWN" : "UP");
            return false;
        }
        // Log vertical swap approval details.
        if (toCell.material_type != Material::EnumType::Air) {
            LOG_INFO(
                Swap,
                "Vertical swap OK: {} -> {} at ({},{}) -> ({},{}) | momentum: {:.3f} (mass: "
                "{:.3f}, vel: {:.3f}, buoyancy: {:.3f}) | resistance: {:.3f} (mass: {:.3f}, "
                "cohesion: {:.3f}, opposing: {:.3f}, com_dist: {:.2f}, com_mult: {:.1f}) | "
                "threshold: {:.3f} | dir.y: {} ({})",
                toString(fromCell.material_type),
                toString(toCell.material_type),
                fromX,
                fromY,
                toX,
                toY,
                effective_momentum,
                from_mass,
                from_velocity,
                buoyancy_boost,
                to_resistance,
                to_mass,
                cohesion_strength,
                opposing_momentum,
                com_distance,
                com_resistance_multiplier,
                to_resistance * threshold,
                direction.y,
                direction.y > 0 ? "DOWN" : "UP");
        }
    }

    // Check cohesion resistance.
    const double cohesion_strength = calculateCohesionStrength(fromCell, world, fromX, fromY);
    double bond_breaking_cost = cohesion_strength
        * world.getPhysicsSettings().cohesion_resistance_factor; // Modified for fluids.

    // Reduce bond cost for fluid interactions (fluids help separate materials).
    if (from_props.is_fluid || to_props.is_fluid) {
        bond_breaking_cost *= world.getPhysicsSettings().fluid_lubrication_factor;
    }

    if (cohesion_strength > 0.01) {
        LOG_DEBUG(
            Swap,
            "Cohesion check: {} at ({},{}) | strength: {:.3f}, bond_cost: {:.3f} (fluid_adjusted)",
            toString(fromCell.material_type),
            fromX,
            fromY,
            cohesion_strength,
            bond_breaking_cost);
    }

    // Calculate swap cost: energy to accelerate target cell's contents to 1 cell/second.
    const double target_mass = toCell.getEffectiveDensity();
    constexpr double SWAP_COST_SCALAR = 1;
    double swap_cost = SWAP_COST_SCALAR * 0.5 * target_mass
        * 1.0; // KE = 0.5 * m * v^2, v = 1.0. Modified for horizontal non-fluids.

    // Horizontal non-fluid swaps require more energy (prevents dirt from flowing sideways too
    // easily). Vertical swaps (buoyancy) should NOT be penalized - density difference drives those.
    if (direction.y == 0 && (!from_props.is_fluid || !to_props.is_fluid)) {
        swap_cost *= world.getPhysicsSettings().horizontal_non_fluid_energy_multiplier;
    }

    // Total cost includes base swap cost + bond breaking cost.
    const double total_cost = swap_cost + bond_breaking_cost;
    double available_energy = move.collision_energy; // Modified by buoyancy.

    // Add buoyancy energy for vertical swaps driven by density differences.
    // Light materials rising or heavy materials sinking get "free" energy from buoyancy.
    if (direction.y != 0) {
        const double vert_density_diff = std::abs(from_props.density - to_props.density);
        const bool is_buoyancy_driven = densitySupportsSwap(fromCell, toCell, direction);

        if (is_buoyancy_driven && vert_density_diff > 0.1) {
            const double buoyancy_energy =
                vert_density_diff * world.getPhysicsSettings().buoyancy_energy_scale;
            available_energy += buoyancy_energy;

            LOG_DEBUG(
                Swap,
                "Buoyancy boost: {} <-> {} | density_diff: {:.3f}, buoyancy_energy: {:.3f}, total: "
                "{:.3f}",
                toString(fromCell.material_type),
                toString(toCell.material_type),
                vert_density_diff,
                buoyancy_energy,
                available_energy);
        }
    }

    if (available_energy < total_cost) {
        if (bond_breaking_cost > 0.01) {
            LOG_DEBUG(
                Swap,
                "Swap denied: insufficient energy to break cohesive bonds ({:.3f} < {:.3f}, "
                "bond_cost: {:.3f})",
                available_energy,
                total_cost,
                bond_breaking_cost);
        }
        else {
            LOG_DEBUG(
                Swap,
                "Swap denied: insufficient energy ({:.3f} < {:.3f})",
                available_energy,
                total_cost);
        }
        return false;
    }

    if (toCell.material_type == Material::EnumType::Air) {
        LOG_DEBUG(
            Swap,
            "Swap approved: {} -> {} at ({},{}) -> ({},{}) | Energy: {:.3f} >= {:.3f} (base: "
            "{:.3f}, bonds: {:.3f}) | Dir: ({},{}) {}",
            toString(fromCell.material_type),
            toString(toCell.material_type),
            fromX,
            fromY,
            fromX + direction.x,
            fromY + direction.y,
            available_energy,
            total_cost,
            swap_cost,
            bond_breaking_cost,
            direction.x,
            direction.y,
            direction.y > 0 ? "DOWN"
                            : (direction.y < 0 ? "UP" : (direction.x > 0 ? "RIGHT" : "LEFT")));
    }
    else {

        LOG_INFO(
            Swap,
            "Swap approved: {} -> {} at ({},{}) -> ({},{}) | Energy: {:.3f} >= {:.3f} (base: "
            "{:.3f}, bonds: {:.3f}) | Dir: ({},{}) {}",
            toString(fromCell.material_type),
            toString(toCell.material_type),
            fromX,
            fromY,
            fromX + direction.x,
            fromY + direction.y,
            available_energy,
            total_cost,
            swap_cost,
            bond_breaking_cost,
            direction.x,
            direction.y,
            direction.y > 0 ? "DOWN"
                            : (direction.y < 0 ? "UP" : (direction.x > 0 ? "RIGHT" : "LEFT")));
    }

    return true;
}

void WorldCollisionCalculator::swapCounterMovingMaterials(
    Cell& fromCell, Cell& toCell, const Vector2i& direction, const MaterialMove& move)
{
    // Store material types before swap for logging.
    const Material::EnumType from_type = fromCell.material_type;
    const Material::EnumType to_type = toCell.material_type;

    // AIR swaps preserve momentum - no real collision occurred.
    // Moving through air should not cost energy (air resistance handled elsewhere).
    const bool involves_air =
        from_type == Material::EnumType::Air || to_type == Material::EnumType::Air;

    Vector2d new_velocity;
    double swap_cost = 0.0;
    double remaining_energy = 0.0;

    if (involves_air) {
        // Preserve full momentum when swapping with air.
        new_velocity = move.momentum;
    }
    else {
        // Calculate swap cost for real material-material swaps.
        // Note: getEffectiveDensity() already includes fill_ratio, so don't multiply again.
        const double target_mass = toCell.getEffectiveDensity();
        swap_cost = 0.5 * target_mass * 1.0;

        // Calculate remaining energy after swap.
        // Energy is only lost proportional to work done (swap_cost).
        remaining_energy = std::max(0.0, move.collision_energy - swap_cost);

        // Get mass of moving material (fromCell -> toCell).
        const double moving_mass = fromCell.getEffectiveDensity();

        // Calculate new velocity magnitude for moving material after energy deduction.
        double velocity_magnitude_new = 0.0;
        if (moving_mass > 1e-6 && remaining_energy > 0.0) {
            velocity_magnitude_new = std::sqrt(2.0 * remaining_energy / moving_mass);
        }

        // Preserve velocity direction, but reduce magnitude.
        const Vector2f velocity_direction =
            move.momentum.magnitude() > 1e-6f ? move.momentum.normalize() : Vector2f(0.0f, 0.0f);
        new_velocity = Vector2d(velocity_direction) * velocity_magnitude_new;
    }

    // Swap material types and fill ratios (conserve mass).
    // Note: organism tracking is handled by OrganismManager, not Cell.
    const Material::EnumType temp_type = fromCell.material_type;
    const double temp_fill = fromCell.fill_ratio;

    fromCell.material_type = toCell.material_type;
    fromCell.fill_ratio = toCell.fill_ratio;

    toCell.material_type = temp_type;
    toCell.fill_ratio = temp_fill;

    // Moving material (now in toCell) continues trajectory with reduced velocity.
    // Calculate landing position based on boundary crossing trajectory.
    const Vector2d landing_com =
        fromCell.calculateTrajectoryLanding(fromCell.com, move.momentum, move.getDirection());
    toCell.setCOM(landing_com);
    toCell.velocity = new_velocity;

    // Displaced material (now in fromCell) receives opposing velocity from buoyancy.
    // Lighter materials pushed up by heavier materials get upward momentum.
    // Place COM at the entering edge - displaced material crossed from the swap direction.
    // This ensures COM distance factor blocks immediate re-swaps.
    constexpr double BOUNDARY_OFFSET = 0.95;
    const Vector2d displaced_com(
        direction.x != 0 ? direction.x * BOUNDARY_OFFSET : 0.0,
        direction.y != 0 ? direction.y * BOUNDARY_OFFSET : 0.0);
    fromCell.setCOM(displaced_com);

    const Material::Properties& displaced_props = Material::getProperties(to_type);
    const Material::Properties& pusher_props = Material::getProperties(from_type);
    const double density_diff = std::abs(pusher_props.density - displaced_props.density);

    // Buoyancy gives the displaced material velocity opposing the swap direction.
    // Use density difference directly as a simple buoyancy model.
    // Scale tuned so displaced material resists cascading swaps.
    constexpr double BUOYANCY_VELOCITY_SCALE = 10.0;
    const double buoyancy_velocity = density_diff * BUOYANCY_VELOCITY_SCALE;
    const Vector2d opposing_dir(-direction.x, -direction.y);
    fromCell.velocity = opposing_dir * buoyancy_velocity;

    // Log with full details, INFO for non-air swaps, DEBUG for air swaps.
    const char* const direction_str =
        direction.y > 0 ? "DOWN" : (direction.y < 0 ? "UP" : (direction.x > 0 ? "RIGHT" : "LEFT"));

    if (involves_air) {
        LOG_DEBUG(
            Swap,
            "SWAP: {} <-> {} at ({},{}) <-> ({},{}) Dir:({},{}) {} | Vel: {:.3f} -> {:.3f} "
            "(air swap, momentum preserved) | landing_com: ({:.2f},{:.2f})",
            toString(from_type),
            toString(to_type),
            move.from.x,
            move.from.y,
            move.to.x,
            move.to.y,
            direction.x,
            direction.y,
            direction_str,
            move.momentum.magnitude(),
            new_velocity.magnitude(),
            landing_com.x,
            landing_com.y);
    }
    else {
        LOG_INFO(
            Swap,
            "SWAP: {} <-> {} at ({},{}) <-> ({},{}) Dir:({},{}) {} | Energy: {:.3f} - {:.3f} = "
            "{:.3f} | Vel: {:.3f} -> {:.3f} | landing_com: ({:.2f},{:.2f})",
            toString(from_type),
            toString(to_type),
            move.from.x,
            move.from.y,
            move.to.x,
            move.to.y,
            direction.x,
            direction.y,
            direction_str,
            move.collision_energy,
            swap_cost,
            remaining_energy,
            move.momentum.magnitude(),
            new_velocity.magnitude(),
            landing_com.x,
            landing_com.y);
    }
}

// =================================================================
// UTILITY METHODS.
// =================================================================

WorldCollisionCalculator::VelocityComponents WorldCollisionCalculator::decomposeVelocity(
    const Vector2d& velocity, const Vector2d& surface_normal) const
{
    VelocityComponents components;
    const Vector2d normalized_normal = surface_normal.normalize();
    components.normal_scalar = velocity.dot(normalized_normal);
    components.normal = normalized_normal * components.normal_scalar;
    components.tangential = velocity - components.normal;
    return components;
}

double WorldCollisionCalculator::calculateCohesionStrength(
    const Cell& cell, const World& world, int x, int y) const
{
    if (cell.isEmpty()) {
        return 0.0;
    }

    // Reuse existing cohesion calculation that includes support factor.
    const WorldCohesionCalculator cohesion_calc;
    const auto cohesion_force = cohesion_calc.calculateCohesionForce(world, x, y);

    // Return the resistance magnitude (includes neighbors, fill ratio, and support factor).
    return cohesion_force.resistance_magnitude;
}
