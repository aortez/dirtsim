#pragma once

#include "core/ReflectSerializer.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsManager {

enum class ScannerBand { Band24Ghz = 0, Band5Ghz };

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

inline void to_json(nlohmann::json& j, const ScannerTuning& tuning)
{
    j = ReflectSerializer::to_json(tuning);
}

inline void from_json(const nlohmann::json& j, ScannerTuning& tuning)
{
    tuning = ReflectSerializer::from_json<ScannerTuning>(j);
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

} // namespace OsManager
} // namespace DirtSim
