#include "WorldAdhesionCalculator.h"
#include "Cell.h"
#include "World.h"
#include "WorldData.h"
#include <cmath>

using namespace DirtSim;

WorldAdhesionCalculator::AdhesionForce WorldAdhesionCalculator::calculateAdhesionForce(
    const World& world, int x, int y) const
{
    const auto& data = world.getData();
    const Cell& cell = data.at(x, y);
    if (cell.isEmpty()) {
        return { { 0.0f, 0.0f }, 0.0f, Material::EnumType::Air, 0 };
    }

    const Material::Properties& props = Material::getProperties(cell.material_type);
    Vector2f total_force(0.0f, 0.0f);
    int contact_count = 0;
    Material::EnumType strongest_attractor = Material::EnumType::Air;
    float max_adhesion = 0.0f;

    // Check all 8 neighbors for different materials.
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;

            const int nx = x + dx;
            const int ny = y + dy;

            if (data.inBounds(nx, ny)) {
                const Cell& neighbor = data.at(nx, ny);

                // Skip same material and AIR neighbors (AIR has adhesion=0.0).
                if (neighbor.material_type == cell.material_type
                    || neighbor.material_type == Material::EnumType::Air) {
                    continue;
                }

                if (neighbor.fill_ratio > MIN_MATTER_THRESHOLD) {

                    // Calculate mutual adhesion (geometric mean).
                    const Material::Properties& neighbor_props =
                        Material::getProperties(neighbor.material_type);
                    const float mutual_adhesion =
                        std::sqrt(props.adhesion * neighbor_props.adhesion);

                    // Direction vector toward neighbor (normalized).
                    Vector2f direction(static_cast<float>(dx), static_cast<float>(dy));
                    direction.normalize();

                    // Force strength weighted by fill ratios and distance.
                    const float distance_weight =
                        (std::abs(dx) + std::abs(dy) == 1) ? 1.0f : 0.707f; // Adjacent vs diagonal.
                    const float force_strength =
                        mutual_adhesion * neighbor.fill_ratio * cell.fill_ratio * distance_weight;

                    total_force += direction * force_strength;
                    contact_count++;

                    if (mutual_adhesion > max_adhesion) {
                        max_adhesion = mutual_adhesion;
                        strongest_attractor = neighbor.material_type;
                    }
                }
            }
        }
    }

    return { total_force, total_force.mag(), strongest_attractor, contact_count };
}

WorldAdhesionCalculator::AdhesionForce WorldAdhesionCalculator::calculateAdhesionForce(
    const World& world, int x, int y, const MaterialNeighborhood& mat_n) const
{
    const auto& data = world.getData();
    const Cell& cell = data.at(x, y);
    if (cell.isEmpty()) {
        return { { 0.0f, 0.0f }, 0.0f, Material::EnumType::Air, 0 };
    }

    const Material::Properties& props = Material::getProperties(cell.material_type);
    const Material::EnumType my_material = mat_n.getCenterMaterial();
    Vector2f total_force(0.0f, 0.0f);
    int contact_count = 0;
    Material::EnumType strongest_attractor = Material::EnumType::Air;
    float max_adhesion = 0.0f;

    // Check all 8 neighbors for different materials.
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;

            const int nx = x + dx;
            const int ny = y + dy;

            // Explicit bounds check - skip out-of-bounds neighbors.
            if (!data.inBounds(nx, ny)) {
                continue;
            }

            // Multi-stage cache filtering (bounds check handled by cache).
            // Stage 1: Material difference check (pure cache - no cell access).
            const Material::EnumType neighbor_material = mat_n.getMaterial(dx, dy);
            if (neighbor_material == my_material || neighbor_material == Material::EnumType::Air) {
                continue;
            }

            // At this point: different material type, guaranteed non-empty due to AIR conversion.
            // Fetch cell only when we know we need it.
            const Cell& neighbor = data.at(nx, ny);

            // Calculate mutual adhesion (geometric mean).
            const Material::Properties& neighbor_props = Material::getProperties(neighbor_material);
            const float mutual_adhesion = std::sqrt(props.adhesion * neighbor_props.adhesion);

            // Direction vector toward neighbor (normalized).
            Vector2f direction(static_cast<float>(dx), static_cast<float>(dy));
            direction.normalize();

            // Force strength weighted by fill ratios and distance.
            const float distance_weight =
                (std::abs(dx) + std::abs(dy) == 1) ? 1.0f : 0.707f; // Adjacent vs diagonal.
            const float force_strength =
                mutual_adhesion * neighbor.fill_ratio * cell.fill_ratio * distance_weight;

            total_force += direction * force_strength;
            contact_count++;

            if (mutual_adhesion > max_adhesion) {
                max_adhesion = mutual_adhesion;
                strongest_attractor = neighbor_material;
            }
        }
    }

    return { total_force, total_force.mag(), strongest_attractor, contact_count };
}