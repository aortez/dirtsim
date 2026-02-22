#pragma once

#include "core/organisms/evolution/NesPolicyLayout.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <array>
#include <cstdint>
#include <string>

namespace DirtSim {

struct NesRomFrameExtraction {
    bool done = false;
    double rewardDelta = 0.0;
    uint8_t gameState = 0;
    std::array<float, NesPolicyLayout::InputCount> features{};
};

class NesRomProfileExtractor {
public:
    explicit NesRomProfileExtractor(std::string romId);

    bool isSupported() const;
    void reset();
    NesRomFrameExtraction extract(
        const SmolnesRuntime::MemorySnapshot& snapshot, uint8_t previousControllerMask);

private:
    enum class Profile : uint8_t {
        Unsupported = 0,
        FlappyParatroopaWorldUnl = 1,
    };

    static std::string normalizeRomId(const std::string& rawRomId);

    Profile profile_ = Profile::Unsupported;
    bool didApplyDeathPenalty_ = false;
    bool hasLastScore_ = false;
    int lastScore_ = 0;
};

} // namespace DirtSim
