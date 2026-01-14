#include "core/bitmaps/MaterialNeighborhood.h"

#include <gtest/gtest.h>

using namespace DirtSim;

/**
 * Test that getMaterial() correctly extracts material types from packed uint64_t.
 */
TEST(MaterialNeighborhoodTest, GetMaterialExtractsCorrectly)
{
    // Pack materials manually: create a 3×3 grid with known materials.
    // Layout:
    //   DIRT  WATER SAND     Bit groups:
    //   WOOD  METAL LEAF     0  1  2
    //   WALL  AIR   SEED     3  4  5
    //                        6  7  8

    uint64_t packed = 0;
    packed |= (static_cast<uint64_t>(Material::EnumType::Dirt) & 0xF) << (0 * 4);  // NW
    packed |= (static_cast<uint64_t>(Material::EnumType::Water) & 0xF) << (1 * 4); // N
    packed |= (static_cast<uint64_t>(Material::EnumType::Sand) & 0xF) << (2 * 4);  // NE
    packed |= (static_cast<uint64_t>(Material::EnumType::Wood) & 0xF) << (3 * 4);  // W
    packed |= (static_cast<uint64_t>(Material::EnumType::Metal) & 0xF) << (4 * 4); // C
    packed |= (static_cast<uint64_t>(Material::EnumType::Leaf) & 0xF) << (5 * 4);  // E
    packed |= (static_cast<uint64_t>(Material::EnumType::Wall) & 0xF) << (6 * 4);  // SW
    packed |= (static_cast<uint64_t>(Material::EnumType::Air) & 0xF) << (7 * 4);   // S
    packed |= (static_cast<uint64_t>(Material::EnumType::Seed) & 0xF) << (8 * 4);  // SE

    MaterialNeighborhood n(packed);

    // Verify each position.
    EXPECT_EQ(n.getMaterial(-1, -1), Material::EnumType::Dirt); // NW
    EXPECT_EQ(n.getMaterial(0, -1), Material::EnumType::Water); // N
    EXPECT_EQ(n.getMaterial(1, -1), Material::EnumType::Sand);  // NE
    EXPECT_EQ(n.getMaterial(-1, 0), Material::EnumType::Wood);  // W
    EXPECT_EQ(n.getMaterial(0, 0), Material::EnumType::Metal);  // C
    EXPECT_EQ(n.getMaterial(1, 0), Material::EnumType::Leaf);   // E
    EXPECT_EQ(n.getMaterial(-1, 1), Material::EnumType::Wall);  // SW
    EXPECT_EQ(n.getMaterial(0, 1), Material::EnumType::Air);    // S
    EXPECT_EQ(n.getMaterial(1, 1), Material::EnumType::Seed);   // SE
}

/**
 * Test named directional accessors.
 */
TEST(MaterialNeighborhoodTest, NamedAccessors)
{
    uint64_t packed = 0;
    packed |= (static_cast<uint64_t>(Material::EnumType::Water) & 0xF) << (1 * 4); // N
    packed |= (static_cast<uint64_t>(Material::EnumType::Dirt) & 0xF) << (7 * 4);  // S
    packed |= (static_cast<uint64_t>(Material::EnumType::Sand) & 0xF) << (5 * 4);  // E
    packed |= (static_cast<uint64_t>(Material::EnumType::Wood) & 0xF) << (3 * 4);  // W
    packed |= (static_cast<uint64_t>(Material::EnumType::Metal) & 0xF) << (4 * 4); // C

    MaterialNeighborhood n(packed);

    EXPECT_EQ(n.north(), Material::EnumType::Water);
    EXPECT_EQ(n.south(), Material::EnumType::Dirt);
    EXPECT_EQ(n.east(), Material::EnumType::Sand);
    EXPECT_EQ(n.west(), Material::EnumType::Wood);
    EXPECT_EQ(n.getCenterMaterial(), Material::EnumType::Metal);
}

/**
 * Test countMaterial() aggregate query.
 */
TEST(MaterialNeighborhoodTest, CountMaterial)
{
    // Create neighborhood with 3 WATER neighbors, 2 DIRT, rest AIR.
    uint64_t packed = 0;
    packed |= (static_cast<uint64_t>(Material::EnumType::Water) & 0xF) << (1 * 4); // N
    packed |= (static_cast<uint64_t>(Material::EnumType::Water) & 0xF) << (3 * 4); // W
    packed |= (static_cast<uint64_t>(Material::EnumType::Water) & 0xF) << (5 * 4); // E
    packed |= (static_cast<uint64_t>(Material::EnumType::Dirt) & 0xF) << (7 * 4);  // S
    packed |= (static_cast<uint64_t>(Material::EnumType::Dirt) & 0xF) << (0 * 4);  // NW
    packed |= (static_cast<uint64_t>(Material::EnumType::Metal) & 0xF) << (4 * 4); // C
    packed |= (static_cast<uint64_t>(Material::EnumType::Air) & 0xF) << (2 * 4);   // NE
    packed |= (static_cast<uint64_t>(Material::EnumType::Air) & 0xF) << (6 * 4);   // SW
    packed |= (static_cast<uint64_t>(Material::EnumType::Air) & 0xF) << (8 * 4);   // SE

    MaterialNeighborhood n(packed);

    EXPECT_EQ(n.countMaterial(Material::EnumType::Water), 3);
    EXPECT_EQ(n.countMaterial(Material::EnumType::Dirt), 2);
    EXPECT_EQ(n.countMaterial(Material::EnumType::Air), 3);
    EXPECT_EQ(n.countMaterial(Material::EnumType::Metal), 0); // Center not counted.
}

/**
 * Test isSurroundedBySameMaterial().
 */
TEST(MaterialNeighborhoodTest, SurroundedBySameMaterial)
{
    // All WATER neighborhood.
    uint64_t all_water = 0;
    for (int i = 0; i < 9; ++i) {
        all_water |= (static_cast<uint64_t>(Material::EnumType::Water) & 0xF) << (i * 4);
    }

    MaterialNeighborhood n1(all_water);
    EXPECT_TRUE(n1.isSurroundedBySameMaterial());

    // Mixed neighborhood.
    uint64_t mixed = all_water;
    mixed &= ~(0xFULL << (1 * 4)); // Clear north.
    mixed |= (static_cast<uint64_t>(Material::EnumType::Dirt) & 0xF)
        << (1 * 4); // Set north to DIRT.

    MaterialNeighborhood n2(mixed);
    EXPECT_FALSE(n2.isSurroundedBySameMaterial());
}
