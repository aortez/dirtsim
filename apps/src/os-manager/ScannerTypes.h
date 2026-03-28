#pragma once

#include "core/ReflectSerializer.h"
#include "core/Result.h"
#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsManager {

enum class ScannerBand { Band24Ghz = 0, Band5Ghz };
enum class ScannerConfigMode { Auto = 0, Manual };
enum class ScannerObservationKind { Direct = 0, Incidental };

inline constexpr std::array<int, 11> kScannerBand24PrimaryChannels{ {
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
} };

inline constexpr std::array<int, 25> kScannerBand5PrimaryChannels{ {
    36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116,
    120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165,
} };

inline constexpr std::array<int, 12> kScannerBand5_40MhzCenterChannels{ {
    38,
    46,
    54,
    62,
    102,
    110,
    118,
    126,
    134,
    142,
    151,
    159,
} };

inline constexpr std::array<int, 6> kScannerBand5_80MhzCenterChannels{ {
    42,
    58,
    106,
    122,
    138,
    155,
} };

template <size_t N>
inline std::vector<int> scannerChannelVector(const std::array<int, N>& channels)
{
    return { channels.begin(), channels.end() };
}

inline bool scannerChannelListContains(const std::vector<int>& channels, const int channel)
{
    return std::find(channels.begin(), channels.end(), channel) != channels.end();
}

inline std::string scannerChannelSpanLabel(const std::vector<int>& channels, const int widthMhz)
{
    if (channels.empty()) {
        return "";
    }

    if (channels.size() == 1) {
        return std::to_string(channels.front());
    }

    if (widthMhz == 40 && channels.size() == 2) {
        return std::to_string(channels.front()) + "+" + std::to_string(channels.back());
    }

    return std::to_string(channels.front()) + "-" + std::to_string(channels.back());
}

inline void to_json(nlohmann::json& j, const ScannerBand& band)
{
    switch (band) {
        case ScannerBand::Band24Ghz:
            j = "2.4GHz";
            return;
        case ScannerBand::Band5Ghz:
            j = "5GHz";
            return;
    }

    throw std::runtime_error("Unhandled ScannerBand");
}

inline void from_json(const nlohmann::json& j, ScannerBand& band)
{
    const std::string text = j.get<std::string>();
    if (text == "2.4GHz") {
        band = ScannerBand::Band24Ghz;
        return;
    }
    if (text == "5GHz") {
        band = ScannerBand::Band5Ghz;
        return;
    }

    throw std::runtime_error("Invalid ScannerBand: " + text);
}

inline void to_json(nlohmann::json& j, const ScannerConfigMode& mode)
{
    switch (mode) {
        case ScannerConfigMode::Auto:
            j = "Auto";
            return;
        case ScannerConfigMode::Manual:
            j = "Manual";
            return;
    }

    throw std::runtime_error("Unhandled ScannerConfigMode");
}

inline void from_json(const nlohmann::json& j, ScannerConfigMode& mode)
{
    const std::string text = j.get<std::string>();
    if (text == "Auto") {
        mode = ScannerConfigMode::Auto;
        return;
    }
    if (text == "Manual") {
        mode = ScannerConfigMode::Manual;
        return;
    }

    throw std::runtime_error("Invalid ScannerConfigMode: " + text);
}

inline void to_json(nlohmann::json& j, const ScannerObservationKind& kind)
{
    switch (kind) {
        case ScannerObservationKind::Direct:
            j = "Direct";
            return;
        case ScannerObservationKind::Incidental:
            j = "Incidental";
            return;
    }

    throw std::runtime_error("Unhandled ScannerObservationKind");
}

inline void from_json(const nlohmann::json& j, ScannerObservationKind& kind)
{
    const std::string text = j.get<std::string>();
    if (text == "Direct") {
        kind = ScannerObservationKind::Direct;
        return;
    }
    if (text == "Incidental") {
        kind = ScannerObservationKind::Incidental;
        return;
    }

    throw std::runtime_error("Invalid ScannerObservationKind: " + text);
}

inline int scannerDefaultWidthMhz(ScannerBand /*band*/)
{
    return 20;
}

inline bool scannerWidthSupported(ScannerBand band, int widthMhz)
{
    switch (band) {
        case ScannerBand::Band24Ghz:
            return widthMhz == 20;
        case ScannerBand::Band5Ghz:
            return widthMhz == 20 || widthMhz == 40 || widthMhz == 80;
    }

    return false;
}

struct ScannerTuning {
    ScannerBand band = ScannerBand::Band5Ghz;
    int primaryChannel = 0;
    int widthMhz = 20;
    std::optional<int> centerChannel;

    using serialize = zpp::bits::members<4>;
};

inline bool operator==(const ScannerTuning& lhs, const ScannerTuning& rhs)
{
    return lhs.band == rhs.band && lhs.primaryChannel == rhs.primaryChannel
        && lhs.widthMhz == rhs.widthMhz && lhs.centerChannel == rhs.centerChannel;
}

inline std::vector<int> scannerBandPrimaryChannels(const ScannerBand band)
{
    switch (band) {
        case ScannerBand::Band24Ghz:
            return scannerChannelVector(kScannerBand24PrimaryChannels);
        case ScannerBand::Band5Ghz:
            return scannerChannelVector(kScannerBand5PrimaryChannels);
    }

    return {};
}

inline std::vector<int> scannerTuningCoveredPrimaryChannels(const ScannerTuning& tuning)
{
    if (tuning.widthMhz <= 20) {
        return { tuning.primaryChannel };
    }

    if (tuning.band != ScannerBand::Band5Ghz || !tuning.centerChannel.has_value()) {
        return { tuning.primaryChannel };
    }

    switch (tuning.widthMhz) {
        case 40:
            return { tuning.centerChannel.value() - 2, tuning.centerChannel.value() + 2 };
        case 80:
            return {
                tuning.centerChannel.value() - 6,
                tuning.centerChannel.value() - 2,
                tuning.centerChannel.value() + 2,
                tuning.centerChannel.value() + 6,
            };
        default:
            return { tuning.primaryChannel };
    }
}

inline bool scannerTuningIncludesPrimaryChannel(const ScannerTuning& tuning, const int channel)
{
    const auto coveredChannels = scannerTuningCoveredPrimaryChannels(tuning);
    return std::find(coveredChannels.begin(), coveredChannels.end(), channel)
        != coveredChannels.end();
}

inline ScannerObservationKind scannerObservationKindForPrimaryChannel(
    const ScannerTuning& tuning, const int channel)
{
    return scannerTuningIncludesPrimaryChannel(tuning, channel)
        ? ScannerObservationKind::Direct
        : ScannerObservationKind::Incidental;
}

inline bool scannerTuningIncludesObservedChannel(const ScannerTuning& tuning, const int channel)
{
    if (scannerTuningIncludesPrimaryChannel(tuning, channel)) {
        return true;
    }

    return tuning.centerChannel.has_value() && tuning.centerChannel.value() == channel;
}

inline ScannerObservationKind scannerObservationKindForObservedChannel(
    const ScannerTuning& tuning, const int channel)
{
    return scannerTuningIncludesObservedChannel(tuning, channel)
        ? ScannerObservationKind::Direct
        : ScannerObservationKind::Incidental;
}

struct ScannerAutoConfig {
    ScannerBand band = ScannerBand::Band5Ghz;
    int widthMhz = 20;

    using serialize = zpp::bits::members<2>;
};

inline bool operator==(const ScannerAutoConfig& lhs, const ScannerAutoConfig& rhs)
{
    return lhs.band == rhs.band && lhs.widthMhz == rhs.widthMhz;
}

struct ScannerManualConfig {
    ScannerBand band = ScannerBand::Band5Ghz;
    int widthMhz = 20;
    int targetChannel = 36;

    using serialize = zpp::bits::members<3>;
};

inline bool operator==(const ScannerManualConfig& lhs, const ScannerManualConfig& rhs)
{
    return lhs.band == rhs.band && lhs.widthMhz == rhs.widthMhz
        && lhs.targetChannel == rhs.targetChannel;
}

struct ScannerConfig {
    ScannerConfigMode mode = ScannerConfigMode::Auto;
    ScannerAutoConfig autoConfig;
    ScannerManualConfig manualConfig;

    using serialize = zpp::bits::members<3>;
};

inline bool operator==(const ScannerConfig& lhs, const ScannerConfig& rhs)
{
    return lhs.mode == rhs.mode && lhs.autoConfig == rhs.autoConfig
        && lhs.manualConfig == rhs.manualConfig;
}

inline void to_json(nlohmann::json& j, const ScannerTuning& tuning)
{
    j = ReflectSerializer::to_json(tuning);
}

inline void from_json(const nlohmann::json& j, ScannerTuning& tuning)
{
    tuning = ReflectSerializer::from_json<ScannerTuning>(j);
}

inline void to_json(nlohmann::json& j, const ScannerAutoConfig& config)
{
    j = ReflectSerializer::to_json(config);
}

inline void from_json(const nlohmann::json& j, ScannerAutoConfig& config)
{
    config = ReflectSerializer::from_json<ScannerAutoConfig>(j);
}

inline void to_json(nlohmann::json& j, const ScannerManualConfig& config)
{
    j = ReflectSerializer::to_json(config);
}

inline void from_json(const nlohmann::json& j, ScannerManualConfig& config)
{
    config = ReflectSerializer::from_json<ScannerManualConfig>(j);
}

inline void to_json(nlohmann::json& j, const ScannerConfig& config)
{
    j = ReflectSerializer::to_json(config);
}

inline void from_json(const nlohmann::json& j, ScannerConfig& config)
{
    config = ReflectSerializer::from_json<ScannerConfig>(j);
}

inline std::optional<ScannerBand> scannerBandFromChannel(int channel)
{
    if (channel >= 1 && channel <= 14) {
        return ScannerBand::Band24Ghz;
    }
    if (channel >= 32) {
        return ScannerBand::Band5Ghz;
    }

    return std::nullopt;
}

inline std::string scannerBandLabel(ScannerBand band)
{
    switch (band) {
        case ScannerBand::Band24Ghz:
            return "2.4 GHz";
        case ScannerBand::Band5Ghz:
            return "5 GHz";
    }

    return "Unknown";
}

inline Result<ScannerTuning, std::string> scannerManualTargetToTuning(
    const ScannerManualConfig& config);

inline std::vector<int> scannerManualTargetChannels(const ScannerBand band, const int widthMhz)
{
    switch (band) {
        case ScannerBand::Band24Ghz:
            if (widthMhz != 20) {
                return {};
            }
            return scannerChannelVector(kScannerBand24PrimaryChannels);
        case ScannerBand::Band5Ghz:
            switch (widthMhz) {
                case 20:
                    return scannerChannelVector(kScannerBand5PrimaryChannels);
                case 40:
                    return scannerChannelVector(kScannerBand5_40MhzCenterChannels);
                case 80:
                    return scannerChannelVector(kScannerBand5_80MhzCenterChannels);
                default:
                    return {};
            }
    }

    return {};
}

inline std::optional<int> scannerManualTargetForPrimaryChannel(
    const ScannerBand band, const int widthMhz, const int primaryChannel)
{
    const auto supportedTargets = scannerManualTargetChannels(band, widthMhz);
    if (supportedTargets.empty()) {
        return std::nullopt;
    }

    if (widthMhz == 20) {
        const auto it = std::find(supportedTargets.begin(), supportedTargets.end(), primaryChannel);
        if (it == supportedTargets.end()) {
            return std::nullopt;
        }

        return primaryChannel;
    }

    for (const int targetChannel : supportedTargets) {
        const auto tuningResult = scannerManualTargetToTuning(
            ScannerManualConfig{
                .band = band,
                .widthMhz = widthMhz,
                .targetChannel = targetChannel,
            });
        if (!tuningResult.isValue()) {
            continue;
        }

        if (scannerTuningIncludesPrimaryChannel(tuningResult.value(), primaryChannel)) {
            return targetChannel;
        }
    }

    return std::nullopt;
}

inline std::string scannerManualTargetShortLabel(
    const ScannerBand band, const int widthMhz, const int targetChannel)
{
    if (widthMhz == 20) {
        return "Ch " + std::to_string(targetChannel);
    }

    switch (band) {
        case ScannerBand::Band24Ghz:
        case ScannerBand::Band5Ghz:
            return "Center " + std::to_string(targetChannel);
    }

    return "Target " + std::to_string(targetChannel);
}

inline std::string scannerManualTargetLabel(
    const ScannerBand band, const int widthMhz, const int targetChannel)
{
    if (widthMhz == 20) {
        return scannerManualTargetShortLabel(band, widthMhz, targetChannel);
    }

    if (band != ScannerBand::Band5Ghz) {
        return scannerManualTargetShortLabel(band, widthMhz, targetChannel);
    }

    const auto coveredChannels = scannerTuningCoveredPrimaryChannels(
        ScannerTuning{
            .band = band,
            .primaryChannel = targetChannel,
            .widthMhz = widthMhz,
            .centerChannel = targetChannel,
        });
    if (!coveredChannels.empty()) {
        return "Center " + std::to_string(targetChannel) + " ("
            + scannerChannelSpanLabel(coveredChannels, widthMhz) + ")";
    }

    return scannerManualTargetShortLabel(band, widthMhz, targetChannel);
}

inline Result<ScannerTuning, std::string> scannerManualTargetToTuning(
    const ScannerManualConfig& config)
{
    if (!scannerWidthSupported(config.band, config.widthMhz)) {
        return Result<ScannerTuning, std::string>::error(
            "Unsupported " + scannerBandLabel(config.band) + " scanner width "
            + std::to_string(config.widthMhz) + " MHz");
    }

    const auto supportedTargets = scannerManualTargetChannels(config.band, config.widthMhz);
    const auto supportedIt =
        std::find(supportedTargets.begin(), supportedTargets.end(), config.targetChannel);
    if (supportedIt == supportedTargets.end()) {
        return Result<ScannerTuning, std::string>::error(
            "Unsupported " + scannerBandLabel(config.band) + " manual target "
            + std::to_string(config.targetChannel) + " @ " + std::to_string(config.widthMhz)
            + " MHz");
    }

    if (config.widthMhz == 20) {
        return Result<ScannerTuning, std::string>::okay(
            ScannerTuning{
                .band = config.band,
                .primaryChannel = config.targetChannel,
                .widthMhz = 20,
                .centerChannel = std::nullopt,
            });
    }

    if (config.band != ScannerBand::Band5Ghz) {
        return Result<ScannerTuning, std::string>::error(
            "Unsupported manual tuning target " + std::to_string(config.targetChannel));
    }

    const auto coveredChannels = scannerTuningCoveredPrimaryChannels(
        ScannerTuning{
            .band = config.band,
            .primaryChannel = config.targetChannel,
            .widthMhz = config.widthMhz,
            .centerChannel = config.targetChannel,
        });
    if (coveredChannels.empty()) {
        return Result<ScannerTuning, std::string>::error(
            "Unsupported manual tuning target " + std::to_string(config.targetChannel));
    }

    const auto supportedPrimaryChannels = scannerBandPrimaryChannels(config.band);
    for (const int channel : coveredChannels) {
        if (!scannerChannelListContains(supportedPrimaryChannels, channel)) {
            return Result<ScannerTuning, std::string>::error(
                "Unsupported manual tuning target " + std::to_string(config.targetChannel));
        }
    }

    return Result<ScannerTuning, std::string>::okay(
        ScannerTuning{
            .band = config.band,
            .primaryChannel = coveredChannels.front(),
            .widthMhz = config.widthMhz,
            .centerChannel = config.targetChannel,
        });
}

inline ScannerConfig scannerDefaultConfig()
{
    return ScannerConfig{
        .mode = ScannerConfigMode::Auto,
        .autoConfig =
            ScannerAutoConfig{
                .band = ScannerBand::Band5Ghz,
                .widthMhz = 20,
            },
        .manualConfig =
            ScannerManualConfig{
                .band = ScannerBand::Band5Ghz,
                .widthMhz = 20,
                .targetChannel = 36,
            },
    };
}

inline ScannerBand scannerConfigBand(const ScannerConfig& config)
{
    if (config.mode == ScannerConfigMode::Manual) {
        return config.manualConfig.band;
    }

    return config.autoConfig.band;
}

inline int scannerConfigWidthMhz(const ScannerConfig& config)
{
    if (config.mode == ScannerConfigMode::Manual) {
        return config.manualConfig.widthMhz;
    }

    return config.autoConfig.widthMhz;
}

inline std::string scannerConfigSummaryLabel(const ScannerConfig& config)
{
    const std::string prefix = scannerBandLabel(scannerConfigBand(config)) + " / "
        + std::to_string(scannerConfigWidthMhz(config)) + " MHz / ";
    if (config.mode == ScannerConfigMode::Manual) {
        return prefix
            + scannerManualTargetShortLabel(
                   config.manualConfig.band,
                   config.manualConfig.widthMhz,
                   config.manualConfig.targetChannel);
    }

    return prefix + "Auto";
}

} // namespace OsManager
} // namespace DirtSim
