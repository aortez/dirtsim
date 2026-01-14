#include "LightTypes.h"

namespace DirtSim {

std::string getLightTypeName(const Light& light)
{
    return std::visit(
        [](const auto& l) -> std::string {
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_same_v<T, PointLight>) {
                return "PointLight";
            }
            else if constexpr (std::is_same_v<T, SpotLight>) {
                return "SpotLight";
            }
            else if constexpr (std::is_same_v<T, RotatingLight>) {
                return "RotatingLight";
            }
            else {
                return "Unknown";
            }
        },
        light.getVariant());
}

} // namespace DirtSim
