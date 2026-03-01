#include "core/scenarios/nes/NesFlappyParatroopaRamExtractor.h"

#include "core/organisms/evolution/NesPolicyLayout.h"

#include <algorithm>
#include <cctype>

namespace DirtSim {

namespace {
constexpr uint16_t kBirdYFracAddr = 0x00;
constexpr uint16_t kBirdYAddr = 0x01;
constexpr uint16_t kBirdVelocityLoAddr = 0x02;
constexpr uint16_t kBirdVelocityHiAddr = 0x03;
constexpr uint16_t kScrollXAddr = 0x08;
constexpr uint16_t kScrollNtAddr = 0x09;
constexpr uint16_t kGameStateAddr = 0x0A;
constexpr uint16_t kNt0Pipe0GapAddr = 0x12;
constexpr uint16_t kNt0Pipe1GapAddr = 0x13;
constexpr uint16_t kNt1Pipe0GapAddr = 0x14;
constexpr uint16_t kNt1Pipe1GapAddr = 0x15;
constexpr uint16_t kScoreOnesAddr = 0x19;
constexpr uint16_t kScoreTensAddr = 0x1A;
constexpr uint16_t kScoreHundredsAddr = 0x1B;

int decodeScoreDigit(uint8_t value)
{
    return std::min<int>(value, 9);
}

int decodeScore(const SmolnesRuntime::MemorySnapshot& snapshot)
{
    const int ones = decodeScoreDigit(snapshot.cpuRam.at(kScoreOnesAddr));
    const int tens = decodeScoreDigit(snapshot.cpuRam.at(kScoreTensAddr));
    const int hundreds = decodeScoreDigit(snapshot.cpuRam.at(kScoreHundredsAddr));
    return (hundreds * 100) + (tens * 10) + ones;
}
} // namespace

NesFlappyParatroopaRamExtractor::NesFlappyParatroopaRamExtractor(std::string romId)
{
    const std::string normalizedRomId = normalizeRomId(romId);
    if (normalizedRomId == NesPolicyLayout::FlappyParatroopaWorldUnlRomId) {
        profile_ = Profile::FlappyParatroopaWorldUnl;
    }
}

bool NesFlappyParatroopaRamExtractor::isSupported() const
{
    return profile_ != Profile::Unsupported;
}

std::optional<NesFlappyBirdEvaluatorInput> NesFlappyParatroopaRamExtractor::extract(
    const SmolnesRuntime::MemorySnapshot& snapshot, uint8_t previousControllerMask) const
{
    if (profile_ == Profile::Unsupported) {
        return std::nullopt;
    }

    NesFlappyBirdEvaluatorInput output;
    output.previousControllerMask = previousControllerMask;
    output.state.gameState = snapshot.cpuRam.at(kGameStateAddr);
    output.state.birdY = static_cast<float>(snapshot.cpuRam.at(kBirdYAddr));
    output.state.birdYFraction = static_cast<float>(snapshot.cpuRam.at(kBirdYFracAddr));
    output.state.scrollX = snapshot.cpuRam.at(kScrollXAddr);
    output.state.scrollNt = snapshot.cpuRam.at(kScrollNtAddr);
    output.state.nt0Pipe0Gap = snapshot.cpuRam.at(kNt0Pipe0GapAddr);
    output.state.nt0Pipe1Gap = snapshot.cpuRam.at(kNt0Pipe1GapAddr);
    output.state.nt1Pipe0Gap = snapshot.cpuRam.at(kNt1Pipe0GapAddr);
    output.state.nt1Pipe1Gap = snapshot.cpuRam.at(kNt1Pipe1GapAddr);
    output.state.score = decodeScore(snapshot);

    const int8_t birdVelHi = static_cast<int8_t>(snapshot.cpuRam.at(kBirdVelocityHiAddr));
    output.state.birdVelocity = static_cast<float>(birdVelHi)
        + (static_cast<float>(snapshot.cpuRam.at(kBirdVelocityLoAddr)) / 256.0f);
    return output;
}

std::string NesFlappyParatroopaRamExtractor::normalizeRomId(const std::string& rawRomId)
{
    std::string normalized;
    normalized.reserve(rawRomId.size());

    bool pendingSeparator = false;
    for (char ch : rawRomId) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch)) {
            if (pendingSeparator && !normalized.empty() && normalized.back() != '-') {
                normalized.push_back('-');
            }
            normalized.push_back(static_cast<char>(std::tolower(uch)));
            pendingSeparator = false;
            continue;
        }
        pendingSeparator = true;
    }

    while (!normalized.empty() && normalized.back() == '-') {
        normalized.pop_back();
    }

    return normalized;
}

} // namespace DirtSim
