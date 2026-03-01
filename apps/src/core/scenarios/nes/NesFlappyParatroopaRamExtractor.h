#pragma once

#include "core/scenarios/nes/NesFlappyBirdEvaluator.h"
#include "core/scenarios/nes/SmolnesRuntime.h"

#include <cstdint>
#include <optional>
#include <string>

namespace DirtSim {

class NesFlappyParatroopaRamExtractor {
public:
    explicit NesFlappyParatroopaRamExtractor(std::string romId);

    bool isSupported() const;
    std::optional<NesFlappyBirdEvaluatorInput> extract(
        const SmolnesRuntime::MemorySnapshot& snapshot, uint8_t previousControllerMask) const;

private:
    enum class Profile : uint8_t {
        Unsupported = 0,
        FlappyParatroopaWorldUnl = 1,
    };

    static std::string normalizeRomId(const std::string& rawRomId);

    Profile profile_ = Profile::Unsupported;
};

} // namespace DirtSim
