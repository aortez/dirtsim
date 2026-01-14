#include "MaterialFragmentationParams.h"

namespace DirtSim {

FragmentationParams getMaterialFragmentationParams(Material::EnumType type)
{
    FragmentationParams params;

    switch (type) {
        case Material::EnumType::Water:
            // Gentle spreading, not explosive.
            params.radial_bias = 0.6;
            params.min_arc = M_PI / 3.0;
            params.max_arc = M_PI;
            params.edge_speed_factor = 0.1;
            params.base_speed = 0.5;
            break;

        case Material::EnumType::Sand:
            // Granular scatter.
            params.radial_bias = 0.5;
            params.min_arc = M_PI / 4.0;
            params.max_arc = M_PI / 2.0;
            params.edge_speed_factor = 1.0;
            params.base_speed = 0.6;
            break;

        case Material::EnumType::Dirt:
            // Chunky breakup.
            params.radial_bias = 0.4;
            params.min_arc = M_PI / 4.0;
            params.max_arc = M_PI / 2.0;
            params.edge_speed_factor = 0.8;
            params.base_speed = 0.5;
            break;

        case Material::EnumType::Metal:
            // Dramatic shatter.
            params.radial_bias = 0.9;
            params.min_arc = M_PI / 2.0;
            params.max_arc = M_PI;
            params.edge_speed_factor = 1.3;
            params.base_speed = 0.8;
            break;

        case Material::EnumType::Air:
        case Material::EnumType::Leaf:
        case Material::EnumType::Root:
        case Material::EnumType::Seed:
        case Material::EnumType::Wall:
        case Material::EnumType::Wood:
            // Use struct defaults.
            break;
    }

    return params;
}

} // namespace DirtSim
