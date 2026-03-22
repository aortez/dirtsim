#include "os-manager/network/NexmonChannelProtocol.h"
#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

using namespace DirtSim::OsManager;

namespace {

TEST(NexmonChannelProtocolTest, EncodeChanspec20MHzSupportsCurrentScanPlan)
{
    const std::vector<std::pair<int, uint32_t>> cases = {
        { 1, 0x1001 },   { 2, 0x1002 },   { 3, 0x1003 },   { 4, 0x1004 },   { 5, 0x1005 },
        { 6, 0x1006 },   { 7, 0x1007 },   { 8, 0x1008 },   { 9, 0x1009 },   { 10, 0x100a },
        { 11, 0x100b },  { 36, 0xd024 },  { 40, 0xd028 },  { 44, 0xd02c },  { 48, 0xd030 },
        { 149, 0xd095 }, { 153, 0xd099 }, { 157, 0xd09d }, { 161, 0xd0a1 }, { 165, 0xd0a5 },
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
    const auto result = NexmonChannelProtocol::buildSetChanspecPayload(153);

    ASSERT_TRUE(result.isValue());
    const std::vector<uint8_t> expected = {
        0x4e, 0x45, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x01, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x63, 0x68, 0x61, 0x6e, 0x73, 0x70,
        0x65, 0x63, 0x00, 0x99, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    EXPECT_EQ(result.value(), expected);
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
