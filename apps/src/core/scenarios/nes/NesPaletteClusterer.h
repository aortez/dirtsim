#pragma once

#include "core/organisms/DuckSensoryData.h"
#include "core/scenarios/nes/NesPaletteFrame.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace DirtSim {

class NesPaletteClusterer {
public:
    NesPaletteClusterer();

    void reset(const std::string& romId = "");
    void observeFrame(const NesPaletteFrame& frame);

    bool isReady() const;
    uint8_t mapIndex(uint8_t paletteIndex) const;

private:
    void buildClusters();
    void buildFallbackMapping();

    std::optional<uint64_t> lastFrameId_ = std::nullopt;
    std::string romId_;
    bool ready_ = false;
    int observedFrameCount_ = 0;
    std::array<uint64_t, 64> indexCounts_{};
    std::array<uint8_t, 64> indexToCluster_{};
    std::array<uint8_t, 64> fallbackIndexToCluster_{};
};

} // namespace DirtSim
