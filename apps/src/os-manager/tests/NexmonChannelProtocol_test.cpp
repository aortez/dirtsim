#include "os-manager/network/NexmonChannelProtocol.h"
#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

using namespace DirtSim::OsManager;

namespace {

ScannerTuning makeTuning(
    const ScannerBand band,
    const int primaryChannel,
    const int widthMhz,
    const std::optional<int> centerChannel = std::nullopt)
{
    return ScannerTuning{
        .band = band,
        .primaryChannel = primaryChannel,
        .widthMhz = widthMhz,
        .centerChannel = centerChannel,
    };
}

TEST(NexmonChannelProtocolTest, EncodeChanspec20MHzSupportsAllTargetChannels)
{
    const std::vector<std::pair<int, uint32_t>> cases = {
        { 1, 0x1001 },   { 2, 0x1002 },   { 3, 0x1003 },   { 4, 0x1004 },   { 5, 0x1005 },
        { 6, 0x1006 },   { 7, 0x1007 },   { 8, 0x1008 },   { 9, 0x1009 },   { 10, 0x100a },
        { 11, 0x100b },  { 36, 0xd024 },  { 40, 0xd028 },  { 44, 0xd02c },  { 48, 0xd030 },
        { 52, 0xd034 },  { 56, 0xd038 },  { 60, 0xd03c },  { 64, 0xd040 },  { 100, 0xd064 },
        { 104, 0xd068 }, { 108, 0xd06c }, { 112, 0xd070 }, { 116, 0xd074 }, { 120, 0xd078 },
        { 124, 0xd07c }, { 128, 0xd080 }, { 132, 0xd084 }, { 136, 0xd088 }, { 140, 0xd08c },
        { 144, 0xd090 }, { 149, 0xd095 }, { 153, 0xd099 }, { 157, 0xd09d }, { 161, 0xd0a1 },
        { 165, 0xd0a5 },
    };

    for (const auto& [channel, expected] : cases) {
        const auto result = NexmonChannelProtocol::encodeChanspec20MHz(channel);
        ASSERT_TRUE(result.isValue()) << channel;
        EXPECT_EQ(result.value(), expected) << channel;
    }
}

TEST(NexmonChannelProtocolTest, EncodeChanspec20MHzRejectsUnsupportedChannel)
{
    const auto result = NexmonChannelProtocol::encodeChanspec20MHz(12);

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue(), "Unsupported 20 MHz scanner channel 12");
}

TEST(NexmonChannelProtocolTest, BuildGetChanspecPayloadMatchesSpec)
{
    const std::vector<uint8_t> expected = {
        0x4e, 0x45, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x63, 0x68, 0x61, 0x6e, 0x73, 0x70, 0x65, 0x63, 0x00, 0x00, 0x00, 0x00,
    };

    EXPECT_EQ(NexmonChannelProtocol::buildGetChanspecPayload(), expected);
}

TEST(NexmonChannelProtocolTest, BuildSetChanspecPayloadMatchesSpec)
{
    const auto result =
        NexmonChannelProtocol::buildSetChanspecPayload(makeTuning(ScannerBand::Band5Ghz, 153, 20));

    ASSERT_TRUE(result.isValue());
    const std::vector<uint8_t> expected = {
        0x4e, 0x45, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x01, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x63, 0x68, 0x61, 0x6e, 0x73, 0x70,
        0x65, 0x63, 0x00, 0x99, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    EXPECT_EQ(result.value(), expected);
}

TEST(NexmonChannelProtocolTest, EncodeChanspecSupports5GHz40MHz)
{
    const auto result =
        NexmonChannelProtocol::encodeChanspec(makeTuning(ScannerBand::Band5Ghz, 153, 40, 151));

    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value(), 0xd997u);
}

TEST(NexmonChannelProtocolTest, EncodeChanspecSupports5GHz80MHz)
{
    const auto result =
        NexmonChannelProtocol::encodeChanspec(makeTuning(ScannerBand::Band5Ghz, 157, 80, 155));

    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value(), 0xe29bu);
}

TEST(NexmonChannelProtocolTest, EncodeChanspecSupports5GHz40MHzDfsBlock)
{
    const auto result =
        NexmonChannelProtocol::encodeChanspec(makeTuning(ScannerBand::Band5Ghz, 52, 40, 54));

    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value(), 0xd836u);
}

TEST(NexmonChannelProtocolTest, EncodeChanspecSupports5GHz80MHzDfsBlock)
{
    const auto result =
        NexmonChannelProtocol::encodeChanspec(makeTuning(ScannerBand::Band5Ghz, 108, 80, 106));

    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value(), 0xe26au);
}

TEST(NexmonChannelProtocolTest, EncodeChanspecRejectsUnsupported5GHz80PrimaryChannel)
{
    const auto result =
        NexmonChannelProtocol::encodeChanspec(makeTuning(ScannerBand::Band5Ghz, 165, 80));

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue(), "Unsupported 80 MHz scanner channel 165");
}

TEST(NexmonChannelProtocolTest, EncodeChanspecRejectsUnsupported5GHz80CenterChannel)
{
    const auto result =
        NexmonChannelProtocol::encodeChanspec(makeTuning(ScannerBand::Band5Ghz, 149, 80, 171));

    ASSERT_TRUE(result.isError());
    EXPECT_EQ(result.errorValue(), "Unsupported 80 MHz center channel 171");
}

TEST(NexmonChannelProtocolTest, ParseGetChanspecPayloadExtractsValue)
{
    const std::vector<uint8_t> payload = {
        0x4e, 0x45, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xa1, 0xd0, 0x00, 0x00, 0x73, 0x70, 0x65, 0x63, 0x00, 0x00, 0x00, 0x00,
    };

    const auto result = NexmonChannelProtocol::parseGetChanspecPayload(payload);

    ASSERT_TRUE(result.isValue());
    EXPECT_EQ(result.value(), 0xd0a1u);
}

} // namespace
