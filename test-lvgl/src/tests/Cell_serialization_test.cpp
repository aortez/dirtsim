#include "core/Cell.h"
#include <gtest/gtest.h>
#include <zpp_bits.h>

using namespace DirtSim;

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
