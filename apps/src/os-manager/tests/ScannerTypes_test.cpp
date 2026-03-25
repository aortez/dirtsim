#include "os-manager/ScannerTypes.h"
#include <gtest/gtest.h>

using namespace DirtSim::OsManager;

TEST(ScannerTypesTest, ManualTargetToTuningUsesCanonicalPrimaryFor80MHzCenter)
{
    const auto tuningResult = scannerManualTargetToTuning(
        ScannerManualConfig{
            .band = ScannerBand::Band5Ghz,
            .widthMhz = 80,
            .targetChannel = 155,
        });

    ASSERT_TRUE(tuningResult.isValue());
    EXPECT_EQ(tuningResult.value().band, ScannerBand::Band5Ghz);
    EXPECT_EQ(tuningResult.value().widthMhz, 80);
    EXPECT_EQ(tuningResult.value().primaryChannel, 149);
    ASSERT_TRUE(tuningResult.value().centerChannel.has_value());
    EXPECT_EQ(tuningResult.value().centerChannel.value(), 155);
}

TEST(ScannerTypesTest, ManualTargetToTuningSupportsDfsCenters)
{
    const auto tuningResult = scannerManualTargetToTuning(
        ScannerManualConfig{
            .band = ScannerBand::Band5Ghz,
            .widthMhz = 80,
            .targetChannel = 106,
        });

    ASSERT_TRUE(tuningResult.isValue());
    EXPECT_EQ(tuningResult.value().band, ScannerBand::Band5Ghz);
    EXPECT_EQ(tuningResult.value().widthMhz, 80);
    EXPECT_EQ(tuningResult.value().primaryChannel, 100);
    ASSERT_TRUE(tuningResult.value().centerChannel.has_value());
    EXPECT_EQ(tuningResult.value().centerChannel.value(), 106);
}

TEST(ScannerTypesTest, ManualTargetLabelIncludesCoveredSpan)
{
    EXPECT_EQ(scannerManualTargetLabel(ScannerBand::Band5Ghz, 40, 38), "Center 38 (36+40)");
    EXPECT_EQ(scannerManualTargetLabel(ScannerBand::Band5Ghz, 80, 42), "Center 42 (36-48)");
    EXPECT_EQ(scannerManualTargetLabel(ScannerBand::Band5Ghz, 40, 54), "Center 54 (52+56)");
    EXPECT_EQ(scannerManualTargetLabel(ScannerBand::Band5Ghz, 80, 106), "Center 106 (100-112)");
}

TEST(ScannerTypesTest, ConfigSummaryUsesManualTargetLabel)
{
    const ScannerConfig config{
        .mode = ScannerConfigMode::Manual,
        .autoConfig =
            ScannerAutoConfig{
                .band = ScannerBand::Band5Ghz,
                .widthMhz = 20,
            },
        .manualConfig =
            ScannerManualConfig{
                .band = ScannerBand::Band5Ghz,
                .widthMhz = 80,
                .targetChannel = 42,
            },
    };

    EXPECT_EQ(scannerConfigSummaryLabel(config), "5 GHz / 80 MHz / Center 42");
}

TEST(ScannerTypesTest, ObservationKindIsDirectInsideTuningSpan)
{
    const auto tuningResult = scannerManualTargetToTuning(
        ScannerManualConfig{
            .band = ScannerBand::Band5Ghz,
            .widthMhz = 80,
            .targetChannel = 42,
        });

    ASSERT_TRUE(tuningResult.isValue());
    EXPECT_EQ(
        scannerObservationKindForPrimaryChannel(tuningResult.value(), 44),
        ScannerObservationKind::Direct);
    EXPECT_TRUE(scannerTuningIncludesPrimaryChannel(tuningResult.value(), 48));
}

TEST(ScannerTypesTest, ObservationKindIsIncidentalOutsideTuningSpan)
{
    const ScannerTuning tuning{
        .band = ScannerBand::Band5Ghz,
        .primaryChannel = 36,
        .widthMhz = 20,
        .centerChannel = std::nullopt,
    };

    EXPECT_EQ(
        scannerObservationKindForPrimaryChannel(tuning, 40), ScannerObservationKind::Incidental);
    EXPECT_FALSE(scannerTuningIncludesPrimaryChannel(tuning, 44));
}

TEST(ScannerTypesTest, BandPrimaryChannelsIncludeDfsRange)
{
    const auto channels = scannerBandPrimaryChannels(ScannerBand::Band5Ghz);

    EXPECT_NE(std::find(channels.begin(), channels.end(), 52), channels.end());
    EXPECT_NE(std::find(channels.begin(), channels.end(), 104), channels.end());
    EXPECT_NE(std::find(channels.begin(), channels.end(), 144), channels.end());
    EXPECT_EQ(std::find(channels.begin(), channels.end(), 171), channels.end());
}
