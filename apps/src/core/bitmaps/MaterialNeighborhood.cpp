#include "MaterialNeighborhood.h"

namespace DirtSim {

Material::EnumType MaterialNeighborhood::north() const
{
    return getMaterial(0, -1);
}

Material::EnumType MaterialNeighborhood::south() const
{
    return getMaterial(0, 1);
}

Material::EnumType MaterialNeighborhood::east() const
{
    return getMaterial(1, 0);
}

Material::EnumType MaterialNeighborhood::west() const
{
    return getMaterial(-1, 0);
}

Material::EnumType MaterialNeighborhood::northEast() const
{
    return getMaterial(1, -1);
}

Material::EnumType MaterialNeighborhood::northWest() const
{
    return getMaterial(-1, -1);
}

Material::EnumType MaterialNeighborhood::southEast() const
{
    return getMaterial(1, 1);
}

Material::EnumType MaterialNeighborhood::southWest() const
{
    return getMaterial(-1, 1);
}

int MaterialNeighborhood::countMaterial(Material::EnumType material) const
{
    int count = 0;
    for (int i = 0; i < 9; ++i) {
        if (i == 4) continue; // Skip center.
        Material::EnumType mat =
            static_cast<Material::EnumType>((data_ >> (i * BITS_PER_MATERIAL)) & 0xF);
        if (mat == material) {
            ++count;
        }
    }
    return count;
}

bool MaterialNeighborhood::allNeighborsSameMaterial(Material::EnumType material) const
{
    for (int i = 0; i < 9; ++i) {
        if (i == 4) continue; // Skip center.
        Material::EnumType mat =
            static_cast<Material::EnumType>((data_ >> (i * BITS_PER_MATERIAL)) & 0xF);
        if (mat != material) {
            return false;
        }
    }
    return true;
}

bool MaterialNeighborhood::isSurroundedBySameMaterial() const
{
    Material::EnumType center_mat = getCenterMaterial();
    return allNeighborsSameMaterial(center_mat);
}

} // namespace DirtSim
