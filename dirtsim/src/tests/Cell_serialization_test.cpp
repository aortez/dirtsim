#include "core/Cell.h"
#include "core/RenderMessageUtils.h"
#include <gtest/gtest.h>
#include <zpp_bits.h>

using namespace DirtSim;
using namespace DirtSim::RenderMessageUtils;

TEST(CellSerializationTest, BasicCellSerializationWorks)
{
    // Create a cell.
    Cell original;
    original.material_type = MaterialType::DIRT;
    original.fill_ratio = 0.8;

    // Serialize using zpp_bits (same as network protocol).
    std::vector<std::byte> buffer;
    auto out = zpp::bits::out(buffer);
    out(original).or_throw();

    // Deserialize.
    Cell deserialized;
    auto in = zpp::bits::in(buffer);
    in(deserialized).or_throw();

    // Verify basic fields survived serialization.
    EXPECT_EQ(deserialized.material_type, original.material_type);
    EXPECT_DOUBLE_EQ(deserialized.fill_ratio, original.fill_ratio);
}

TEST(CellSerializationTest, DebugCellPackingPreservesValues)
{
    Cell cell;
    cell.material_type = MaterialType::WOOD;
    cell.fill_ratio = 0.8;
    cell.com = Vector2d{ 0.5, -0.3 };
    cell.velocity = Vector2d{ 1.5, -2.0 };
    cell.pressure = 50.0;
    cell.pressure_gradient = Vector2d{ 0.1, -0.2 };

    DebugCell packed = packDebugCell(cell);
    UnpackedDebugCell unpacked = unpackDebugCell(packed);

    EXPECT_EQ(unpacked.material_type, MaterialType::WOOD);
    EXPECT_NEAR(unpacked.fill_ratio, 0.8, 0.01);
    EXPECT_NEAR(unpacked.com.x, 0.5, 0.01);
    EXPECT_NEAR(unpacked.com.y, -0.3, 0.01);
    EXPECT_NEAR(unpacked.velocity.x, 1.5, 0.1);
    EXPECT_NEAR(unpacked.velocity.y, -2.0, 0.1);
    EXPECT_NEAR(unpacked.pressure_hydro, 50.0, 1.0);
    EXPECT_NEAR(unpacked.pressure_gradient.x, 0.1, 0.01);
    EXPECT_NEAR(unpacked.pressure_gradient.y, -0.2, 0.01);
}
