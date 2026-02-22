#include "core/scenarios/nes/NesRomProfileExtractor.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

using namespace DirtSim;

namespace {

struct FixtureRow {
    int frame = 0;
    uint8_t mask = 0;
    uint8_t state = 0;
    uint8_t birdYFrac = 0;
    uint8_t birdY = 0;
    uint8_t birdVelLo = 0;
    uint8_t birdVelHi = 0;
    uint8_t scrollX = 0;
    uint8_t scrollNt = 0;
    uint8_t scoreOnes = 0;
    uint8_t scoreTens = 0;
    uint8_t scoreHundreds = 0;
    uint8_t pipesScored = 0;
    uint8_t birdX = 0;
    uint8_t nt0Pipe0Gap = 0;
    uint8_t nt0Pipe1Gap = 0;
    uint8_t nt1Pipe0Gap = 0;
    uint8_t nt1Pipe1Gap = 0;
};

std::vector<FixtureRow> loadFixtureRows()
{
    const std::filesystem::path fixturePath =
        std::filesystem::path("testdata") / "nes" / "flappy_extractor_fixture.csv";
    std::ifstream stream(fixturePath);
    EXPECT_TRUE(stream.is_open()) << "Missing fixture: " << fixturePath.string();

    std::vector<FixtureRow> rows;
    std::string line;
    std::getline(stream, line); // Skip header.
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        FixtureRow row;
        std::stringstream parser(line);
        std::string token;
        std::vector<int> values;
        while (std::getline(parser, token, ',')) {
            values.push_back(std::stoi(token));
        }

        if (values.size() != 18u) {
            ADD_FAILURE() << "Unexpected fixture column count on line: " << line;
            continue;
        }
        row.frame = values[0];
        row.mask = static_cast<uint8_t>(values[1]);
        row.state = static_cast<uint8_t>(values[2]);
        row.birdYFrac = static_cast<uint8_t>(values[3]);
        row.birdY = static_cast<uint8_t>(values[4]);
        row.birdVelLo = static_cast<uint8_t>(values[5]);
        row.birdVelHi = static_cast<uint8_t>(values[6]);
        row.scrollX = static_cast<uint8_t>(values[7]);
        row.scrollNt = static_cast<uint8_t>(values[8]);
        row.scoreOnes = static_cast<uint8_t>(values[9]);
        row.scoreTens = static_cast<uint8_t>(values[10]);
        row.scoreHundreds = static_cast<uint8_t>(values[11]);
        row.pipesScored = static_cast<uint8_t>(values[12]);
        row.birdX = static_cast<uint8_t>(values[13]);
        row.nt0Pipe0Gap = static_cast<uint8_t>(values[14]);
        row.nt0Pipe1Gap = static_cast<uint8_t>(values[15]);
        row.nt1Pipe0Gap = static_cast<uint8_t>(values[16]);
        row.nt1Pipe1Gap = static_cast<uint8_t>(values[17]);
        rows.push_back(row);
    }

    return rows;
}

SmolnesRuntime::MemorySnapshot makeSnapshot(const FixtureRow& row)
{
    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0);
    snapshot.prgRam.fill(0);

    snapshot.cpuRam[0x00] = row.birdYFrac;
    snapshot.cpuRam[0x01] = row.birdY;
    snapshot.cpuRam[0x02] = row.birdVelLo;
    snapshot.cpuRam[0x03] = row.birdVelHi;
    snapshot.cpuRam[0x08] = row.scrollX;
    snapshot.cpuRam[0x09] = row.scrollNt;
    snapshot.cpuRam[0x0A] = row.state;
    snapshot.cpuRam[0x12] = row.nt0Pipe0Gap;
    snapshot.cpuRam[0x13] = row.nt0Pipe1Gap;
    snapshot.cpuRam[0x14] = row.nt1Pipe0Gap;
    snapshot.cpuRam[0x15] = row.nt1Pipe1Gap;
    snapshot.cpuRam[0x19] = row.scoreOnes;
    snapshot.cpuRam[0x1A] = row.scoreTens;
    snapshot.cpuRam[0x1B] = row.scoreHundreds;
    snapshot.cpuRam[0x1C] = row.pipesScored;
    snapshot.cpuRam[0x20] = row.birdX;
    return snapshot;
}

} // namespace

TEST(NesRomProfileExtractorTest, UnsupportedRomYieldsNoSignals)
{
    NesRomProfileExtractor extractor("unsupported-rom");
    ASSERT_FALSE(extractor.isSupported());

    SmolnesRuntime::MemorySnapshot snapshot;
    snapshot.cpuRam.fill(0);
    snapshot.prgRam.fill(0);

    const NesRomFrameExtraction extraction = extractor.extract(snapshot, 0);
    EXPECT_FALSE(extraction.done);
    EXPECT_DOUBLE_EQ(extraction.rewardDelta, 0.0);
    for (float feature : extraction.features) {
        EXPECT_FLOAT_EQ(feature, 0.0f);
    }
}

TEST(NesRomProfileExtractorTest, FlappyFixtureProducesScoreRewardAndDone)
{
    const std::vector<FixtureRow> rows = loadFixtureRows();
    ASSERT_FALSE(rows.empty());

    NesRomProfileExtractor extractor(NesPolicyLayout::FlappyParatroopaWorldUnlRomId);
    ASSERT_TRUE(extractor.isSupported());

    bool sawScoreReward = false;
    bool sawDone = false;
    double cumulativeReward = 0.0;

    for (const FixtureRow& row : rows) {
        const SmolnesRuntime::MemorySnapshot snapshot = makeSnapshot(row);
        const NesRomFrameExtraction extraction = extractor.extract(snapshot, row.mask);

        cumulativeReward += extraction.rewardDelta;
        if (extraction.rewardDelta > 0.0) {
            sawScoreReward = true;
            EXPECT_DOUBLE_EQ(extraction.rewardDelta, 1.0);
            EXPECT_EQ(row.frame, 347);
        }
        if (extraction.done) {
            sawDone = true;
            EXPECT_EQ(row.state, 3u);
            EXPECT_EQ(row.frame, 393);
            EXPECT_DOUBLE_EQ(extraction.rewardDelta, -1.0);
        }

        EXPECT_EQ(extraction.features.size(), static_cast<size_t>(NesPolicyLayout::InputCount));
        for (float feature : extraction.features) {
            EXPECT_TRUE(std::isfinite(feature));
            EXPECT_GE(feature, -1.0f);
            EXPECT_LE(feature, 1.0f);
        }
    }

    EXPECT_TRUE(sawScoreReward);
    EXPECT_TRUE(sawDone);
    EXPECT_DOUBLE_EQ(cumulativeReward, 0.0);
}
