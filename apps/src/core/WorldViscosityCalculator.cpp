#include "WorldViscosityCalculator.h"
#include "Cell.h"
#include "GridOfCells.h"
#include "World.h"
#include "WorldData.h"
#include <cmath>
#include <spdlog/spdlog.h>

namespace DirtSim {

WorldViscosityCalculator::ViscousForce WorldViscosityCalculator::calculateViscousForce(
    const World& world, int x, int y, float viscosity_strength, const GridOfCells* /*grid*/) const
{
    const WorldData& data = world.getData();
    const Cell& cell = data.at(x, y);

    // Skip empty cells and walls.
    if (cell.isEmpty() || cell.isWall()) {
        return ViscousForce{ .force = { 0.0f, 0.0f },
                             .neighbor_avg_speed = 0.0f,
                             .neighbor_count = 0 };
    }

    // Get material properties.
    const Material::Properties& props = Material::getProperties(cell.material_type);

    // Skip if viscosity is zero.
    if (props.viscosity <= 0.0f) {
        return ViscousForce{ .force = { 0.0f, 0.0f },
                             .neighbor_avg_speed = 0.0f,
                             .neighbor_count = 0 };
    }

    // Calculate weighted average velocity of same-material neighbors.
    Vector2f velocity_sum{ 0.0f, 0.0f };
    float weight_sum = 0.0f;
    int neighbor_count = 0;

    // Check all 8 neighbors.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }

            const int nx = x + dx;
            const int ny = y + dy;

            // Bounds check.
            if (!data.inBounds(nx, ny)) {
                continue;
            }

            const Cell& neighbor = data.at(nx, ny);

            // Only couple with same-material neighbors.
            if (neighbor.material_type != cell.material_type || neighbor.isEmpty()) {
                continue;
            }

            neighbor_count++;

            // Distance weighting (diagonal neighbors are farther).
            const float distance_weight = (dx != 0 && dy != 0) ? 0.707f : 1.0f;

            // Fill ratio weighting (more matter = stronger influence).
            const float fill_weight = neighbor.fill_ratio;

            // Combined weight.
            const float weight = distance_weight * fill_weight;

            velocity_sum += neighbor.velocity * weight;
            weight_sum += weight;
        }
    }

    // No neighbors = no viscous coupling.
    if (neighbor_count == 0) {
        return ViscousForce{ .force = { 0.0f, 0.0f },
                             .neighbor_avg_speed = 0.0f,
                             .neighbor_count = 0 };
    }

    // Calculate average neighbor velocity.
    const Vector2f avg_neighbor_velocity =
        (weight_sum > 0.0f) ? (velocity_sum / weight_sum) : Vector2f{ 0.0f, 0.0f };

    // Velocity difference drives viscous force.
    const Vector2f velocity_difference = avg_neighbor_velocity - cell.velocity;

    // Scale viscosity by connectivity (isolated particles experience less viscous drag).
    const float connectivity_factor = static_cast<float>(neighbor_count) / 8.0f;
    const float effective_viscosity = props.viscosity * connectivity_factor;

    // Viscous force tries to eliminate velocity differences.
    // Scale by viscosity strength (UI control) and fill ratio.
    const Vector2f viscous_force =
        velocity_difference * effective_viscosity * viscosity_strength * cell.fill_ratio;

    // Debug info.
    const float neighbor_avg_speed = avg_neighbor_velocity.magnitude();

    return ViscousForce{ .force = viscous_force,
                         .neighbor_avg_speed = neighbor_avg_speed,
                         .neighbor_count = neighbor_count };
}

} // namespace DirtSim
