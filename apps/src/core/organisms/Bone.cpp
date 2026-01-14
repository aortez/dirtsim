#include "Bone.h"

#include <algorithm>

namespace DirtSim {

double getBoneStiffness(Material::EnumType a, Material::EnumType b)
{
    // Order-independent lookup via sorting.
    if (a > b) std::swap(a, b);

    // Core structure - very stiff.
    if ((a == Material::EnumType::Seed && b == Material::EnumType::Wood)
        || (a == Material::EnumType::Seed && b == Material::EnumType::Root)) {
        return 1.0;
    }

    // Trunk and branches.
    if (a == Material::EnumType::Wood && b == Material::EnumType::Wood) {
        return 0.8;
    }

    // Root system - somewhat flexible.
    if (a == Material::EnumType::Root && b == Material::EnumType::Root) {
        return 0.5;
    }
    if (a == Material::EnumType::Root && b == Material::EnumType::Wood) {
        return 0.6;
    }

    // Foliage - stiff attachment to wood, flexible between leaves.
    if (a == Material::EnumType::Leaf && b == Material::EnumType::Wood) {
        return 3.0; // Strong attachment to prevent leaves from falling.
    }
    if (a == Material::EnumType::Leaf && b == Material::EnumType::Leaf) {
        return 0.1;
    }

    // Default for any other organism material pairs.
    return 0.3;
}

} // namespace DirtSim
