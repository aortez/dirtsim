#include "core/Cell.h"
#include "core/RenderMessage.h"
#include "core/RenderMessageUtils.h"
#include "server/api/TrainingBestPlaybackFrame.h"
#include "server/api/TrainingBestSnapshot.h"
#include <gtest/gtest.h>
#include <zpp_bits.h>

using namespace DirtSim;
using namespace DirtSim::RenderMessageUtils;

TEST(CellSerializationTest, BasicCellSerializationWorks)
{
    // Create a cell.
    Cell original;
    original.material_type = Material::EnumType::Dirt;
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
    cell.material_type = Material::EnumType::Wood;
    cell.fill_ratio = 0.8;
    cell.com = Vector2d{ 0.5, -0.3 };
    cell.velocity = Vector2d{ 1.5, -2.0 };
    cell.pressure = 50.0;
    cell.pressure_gradient = Vector2d{ 0.1, -0.2 };

    DebugCell packed = packDebugCell(cell);
    UnpackedDebugCell unpacked = unpackDebugCell(packed);

    EXPECT_EQ(unpacked.material_type, Material::EnumType::Wood);
    EXPECT_NEAR(unpacked.fill_ratio, 0.8, 0.01);
    EXPECT_NEAR(unpacked.com.x, 0.5, 0.01);
    EXPECT_NEAR(unpacked.com.y, -0.3, 0.01);
    EXPECT_NEAR(unpacked.velocity.x, 1.5, 0.1);
    EXPECT_NEAR(unpacked.velocity.y, -2.0, 0.1);
    EXPECT_NEAR(unpacked.pressure_hydro, 50.0, 1.0);
    EXPECT_NEAR(unpacked.pressure_gradient.x, 0.1, 0.01);
    EXPECT_NEAR(unpacked.pressure_gradient.y, -0.2, 0.01);
}

TEST(CellSerializationTest, RenderMessageSerializationIncludesScenarioVideoFrame)
{
    RenderMessage original;
    original.format = RenderFormat::EnumType::Basic;
    original.width = 47;
    original.height = 30;
    original.timestep = 123;
    original.fps_server = 60.0;

    ScenarioVideoFrame frame;
    frame.width = 256;
    frame.height = 224;
    frame.frame_id = 42;
    frame.pixels = { std::byte{ 0x12 }, std::byte{ 0x34 }, std::byte{ 0xAB }, std::byte{ 0xCD } };
    original.scenario_video_frame = frame;

    std::vector<std::byte> buffer;
    auto out = zpp::bits::out(buffer);
    out(original).or_throw();

    RenderMessage decoded;
    auto in = zpp::bits::in(buffer);
    in(decoded).or_throw();

    ASSERT_TRUE(decoded.scenario_video_frame.has_value());
    EXPECT_EQ(decoded.scenario_video_frame->width, frame.width);
    EXPECT_EQ(decoded.scenario_video_frame->height, frame.height);
    EXPECT_EQ(decoded.scenario_video_frame->frame_id, frame.frame_id);
    EXPECT_EQ(decoded.scenario_video_frame->pixels, frame.pixels);
}

TEST(CellSerializationTest, TrainingBestSnapshotSerializationIncludesScenarioVideoFrame)
{
    Api::TrainingBestSnapshot original;
    original.fitness = 1.5;
    original.generation = 12;

    ScenarioVideoFrame frame;
    frame.width = 256;
    frame.height = 224;
    frame.frame_id = 99;
    frame.pixels = { std::byte{ 0xBE }, std::byte{ 0xEF }, std::byte{ 0xCA }, std::byte{ 0xFE } };
    original.scenarioVideoFrame = frame;

    std::vector<std::byte> buffer;
    auto out = zpp::bits::out(buffer);
    out(original).or_throw();

    Api::TrainingBestSnapshot decoded;
    auto in = zpp::bits::in(buffer);
    in(decoded).or_throw();

    ASSERT_TRUE(decoded.scenarioVideoFrame.has_value());
    EXPECT_EQ(decoded.scenarioVideoFrame->width, frame.width);
    EXPECT_EQ(decoded.scenarioVideoFrame->height, frame.height);
    EXPECT_EQ(decoded.scenarioVideoFrame->frame_id, frame.frame_id);
    EXPECT_EQ(decoded.scenarioVideoFrame->pixels, frame.pixels);
}

TEST(CellSerializationTest, TrainingBestPlaybackFrameSerializationIncludesScenarioVideoFrame)
{
    Api::TrainingBestPlaybackFrame original;
    original.fitness = 3.2;
    original.generation = 7;

    ScenarioVideoFrame frame;
    frame.width = 256;
    frame.height = 224;
    frame.frame_id = 123;
    frame.pixels = { std::byte{ 0xAA }, std::byte{ 0x55 }, std::byte{ 0x12 }, std::byte{ 0x34 } };
    original.scenarioVideoFrame = frame;

    std::vector<std::byte> buffer;
    auto out = zpp::bits::out(buffer);
    out(original).or_throw();

    Api::TrainingBestPlaybackFrame decoded;
    auto in = zpp::bits::in(buffer);
    in(decoded).or_throw();

    ASSERT_TRUE(decoded.scenarioVideoFrame.has_value());
    EXPECT_EQ(decoded.scenarioVideoFrame->width, frame.width);
    EXPECT_EQ(decoded.scenarioVideoFrame->height, frame.height);
    EXPECT_EQ(decoded.scenarioVideoFrame->frame_id, frame.frame_id);
    EXPECT_EQ(decoded.scenarioVideoFrame->pixels, frame.pixels);
}
