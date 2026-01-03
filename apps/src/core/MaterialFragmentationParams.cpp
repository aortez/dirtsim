#include "MaterialFragmentationParams.h"

namespace DirtSim {

FragmentationParams getMaterialFragmentationParams(MaterialType type)
{
    FragmentationParams params;

    switch (type) {
        case MaterialType::WATER:
            // Gentle spreading, not explosive.
            params.radial_bias = 0.6;
            params.min_arc = M_PI / 3.0;
            params.max_arc = M_PI;
            params.edge_speed_factor = 0.1;
            params.base_speed_factor = 0.5;
            break;

        case MaterialType::SAND:
            // Granular scatter.
            params.radial_bias = 0.5;
            params.min_arc = M_PI / 4.0;
            params.max_arc = M_PI / 2.0;
            params.edge_speed_factor = 1.0;
            params.base_speed_factor = 0.6;
            break;

        case MaterialType::DIRT:
            // Chunky breakup.
            params.radial_bias = 0.4;
            params.min_arc = M_PI / 4.0;
            params.max_arc = M_PI / 2.0;
            params.edge_speed_factor = 0.8;
            params.base_speed_factor = 0.5;
            break;

        case MaterialType::METAL:
            // Dramatic shatter.
            params.radial_bias = 0.9;
            params.min_arc = M_PI / 2.0;
            params.max_arc = M_PI;
            params.edge_speed_factor = 1.3;
            params.base_speed_factor = 0.8;
            break;

        default:
            // Use struct defaults.
            break;
    }

    return params;
}

} // namespace DirtSim
