#include "AdaptiveScanPlanner.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace DirtSim {
namespace OsManager {

namespace {

constexpr int kWidth20Mhz = 20;
constexpr int kWidth40Mhz = 40;
constexpr int kWidth80Mhz = 80;

bool scannerTuningMatchesConfig(const ScannerTuning& tuning, const ScannerAutoConfig& config)
{
    return tuning.band == config.band && tuning.widthMhz == config.widthMhz;
}

bool scannerTuningsEqual(const ScannerTuning& a, const ScannerTuning& b)
{
    return a.band == b.band && a.primaryChannel == b.primaryChannel && a.widthMhz == b.widthMhz
        && a.centerChannel == b.centerChannel;
}

ScannerTuning makeScannerTuning(
    const ScannerBand band,
    const int primaryChannel,
    const int widthMhz,
    const std::optional<int> centerChannel)
{
    return ScannerTuning{
        .band = band,
        .primaryChannel = primaryChannel,
        .widthMhz = widthMhz,
        .centerChannel = centerChannel,
    };
}

std::vector<ScannerTuning> supportedScannerTunings()
{
    std::vector<ScannerTuning> tunings;
    tunings.reserve(48);

    for (const int channel : scannerBandPrimaryChannels(ScannerBand::Band24Ghz)) {
        tunings.push_back(
            makeScannerTuning(ScannerBand::Band24Ghz, channel, kWidth20Mhz, std::nullopt));
    }

    for (const int channel : scannerBandPrimaryChannels(ScannerBand::Band5Ghz)) {
        tunings.push_back(
            makeScannerTuning(ScannerBand::Band5Ghz, channel, kWidth20Mhz, std::nullopt));
    }

    // For 40/80 MHz, one tuning per center channel. All primary channels within a center
    // cover the same band, so stepping through them individually is redundant.
    for (const int centerChannel :
         scannerManualTargetChannels(ScannerBand::Band5Ghz, kWidth40Mhz)) {
        const auto coveredChannels = scannerTuningCoveredPrimaryChannels(
            ScannerTuning{
                .band = ScannerBand::Band5Ghz,
                .primaryChannel = centerChannel,
                .widthMhz = kWidth40Mhz,
                .centerChannel = centerChannel,
            });
        if (!coveredChannels.empty()) {
            tunings.push_back(makeScannerTuning(
                ScannerBand::Band5Ghz, coveredChannels.front(), kWidth40Mhz, centerChannel));
        }
    }

    for (const int centerChannel :
         scannerManualTargetChannels(ScannerBand::Band5Ghz, kWidth80Mhz)) {
        const auto coveredChannels = scannerTuningCoveredPrimaryChannels(
            ScannerTuning{
                .band = ScannerBand::Band5Ghz,
                .primaryChannel = centerChannel,
                .widthMhz = kWidth80Mhz,
                .centerChannel = centerChannel,
            });
        if (!coveredChannels.empty()) {
            tunings.push_back(makeScannerTuning(
                ScannerBand::Band5Ghz, coveredChannels.front(), kWidth80Mhz, centerChannel));
        }
    }

    return tunings;
}

int64_t ageMsOrMax(
    const std::optional<std::chrono::steady_clock::time_point>& timePoint,
    const std::chrono::steady_clock::time_point now)
{
    if (!timePoint.has_value()) {
        return std::numeric_limits<int64_t>::max();
    }

    return std::chrono::duration_cast<std::chrono::milliseconds>(now - *timePoint).count();
}

} // namespace

struct AdaptiveScanPlanner::Impl {
    struct TuningState {
        ScannerTuning tuning;
        int consecutiveEmptyVisits = 0;
        size_t lastNewRadiosSeen = 0;
        std::optional<std::chrono::steady_clock::time_point> lastObservedAt;
        std::optional<std::chrono::steady_clock::time_point> lastVisitedAt;
        std::optional<int> strongestSignalDbm;
    };

    explicit Impl(const AdaptiveScanPlannerConfig& config) : config(config)
    {
        for (const auto& tuning : supportedScannerTunings()) {
            tuningStates.push_back(
                TuningState{
                    .tuning = tuning,
                    .consecutiveEmptyVisits = 0,
                    .lastNewRadiosSeen = 0,
                    .lastObservedAt = std::nullopt,
                    .lastVisitedAt = std::nullopt,
                    .strongestSignalDbm = std::nullopt,
                });
        }
    }

    TuningState& tuningStateFor(const ScannerTuning& tuning)
    {
        const auto it = std::find_if(
            tuningStates.begin(), tuningStates.end(), [&tuning](const TuningState& state) {
                return scannerTuningsEqual(state.tuning, tuning);
            });
        return *it;
    }

    const TuningState& tuningStateFor(const ScannerTuning& tuning) const
    {
        const auto it = std::find_if(
            tuningStates.begin(), tuningStates.end(), [&tuning](const TuningState& state) {
                return scannerTuningsEqual(state.tuning, tuning);
            });
        return *it;
    }

    std::vector<TuningState*> focusCandidates()
    {
        std::vector<TuningState*> candidates;
        for (auto& state : tuningStates) {
            if (!scannerTuningMatchesConfig(state.tuning, autoConfig)) {
                continue;
            }
            candidates.push_back(&state);
        }
        return candidates;
    }

    std::vector<const TuningState*> focusCandidates() const
    {
        std::vector<const TuningState*> candidates;
        for (const auto& state : tuningStates) {
            if (!scannerTuningMatchesConfig(state.tuning, autoConfig)) {
                continue;
            }
            candidates.push_back(&state);
        }
        return candidates;
    }

    bool candidatePreferredForDiscovery(
        const TuningState& candidate,
        const TuningState& currentBest,
        const std::chrono::steady_clock::time_point now) const
    {
        const int64_t candidateVisitedAgeMs = ageMsOrMax(candidate.lastVisitedAt, now);
        const int64_t currentVisitedAgeMs = ageMsOrMax(currentBest.lastVisitedAt, now);
        if (candidateVisitedAgeMs != currentVisitedAgeMs) {
            return candidateVisitedAgeMs > currentVisitedAgeMs;
        }

        if (candidate.lastObservedAt.has_value() != currentBest.lastObservedAt.has_value()) {
            return !candidate.lastObservedAt.has_value();
        }

        const int64_t candidateObservedAgeMs = ageMsOrMax(candidate.lastObservedAt, now);
        const int64_t currentObservedAgeMs = ageMsOrMax(currentBest.lastObservedAt, now);
        if (candidateObservedAgeMs != currentObservedAgeMs) {
            return candidateObservedAgeMs > currentObservedAgeMs;
        }

        if (candidate.consecutiveEmptyVisits != currentBest.consecutiveEmptyVisits) {
            return candidate.consecutiveEmptyVisits < currentBest.consecutiveEmptyVisits;
        }

        return candidate.tuning.primaryChannel < currentBest.tuning.primaryChannel;
    }

    bool hasOverdueCandidate(const std::chrono::steady_clock::time_point now) const
    {
        for (const TuningState* state : focusCandidates()) {
            if (!state->lastVisitedAt.has_value()) {
                continue;
            }

            const auto age =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - *state->lastVisitedAt);
            if (age.count() > config.maxRevisitAgeMs) {
                return true;
            }
        }

        return false;
    }

    int jitteredDwellMs(const int baseDwellMs, const int maxJitterMs)
    {
        jitterState = jitterState * 1664525u + 1013904223u;
        const int span = (maxJitterMs * 2) + 1;
        const int jitter =
            static_cast<int>(jitterState % static_cast<uint32_t>(span)) - maxJitterMs;
        return std::clamp(baseDwellMs + jitter, config.minDwellMs, config.maxDwellMs);
    }

    ScannerTuning selectDiscoveryTuning(const std::chrono::steady_clock::time_point now) const
    {
        const auto candidates = focusCandidates();
        const TuningState* bestState = candidates.front();
        for (const TuningState* candidate : candidates) {
            if (candidate == bestState) {
                continue;
            }
            if (candidatePreferredForDiscovery(*candidate, *bestState, now)) {
                bestState = candidate;
            }
        }

        return bestState->tuning;
    }

    ScannerTuning selectTrackingTuning()
    {
        const auto candidates = focusCandidates();
        const size_t index = nextTrackingIndex % candidates.size();
        const ScannerTuning tuning = candidates[index]->tuning;
        nextTrackingIndex = (nextTrackingIndex + 1) % candidates.size();
        return tuning;
    }

    void syncTrackingCursorAfterTuning(const ScannerTuning& tuning)
    {
        const auto candidates = focusCandidates();
        const auto it =
            std::find_if(candidates.begin(), candidates.end(), [&tuning](const TuningState* state) {
                return scannerTuningsEqual(state->tuning, tuning);
            });
        if (it == candidates.end()) {
            nextTrackingIndex = 0;
            return;
        }

        nextTrackingIndex =
            (static_cast<size_t>(std::distance(candidates.begin(), it)) + 1) % candidates.size();
    }

    AdaptiveScanPlannerConfig config;
    ScannerAutoConfig autoConfig{
        .band = ScannerBand::Band5Ghz,
        .widthMhz = kWidth20Mhz,
    };
    size_t nextTrackingIndex = 0;
    std::vector<TuningState> tuningStates;
    int stepsSinceDiscovery = 0;
    uint32_t jitterState = 0;
};

AdaptiveScanPlanner::AdaptiveScanPlanner(AdaptiveScanPlannerConfig config)
    : config_(config), impl_(std::make_unique<Impl>(config_))
{
    reset();
}

AdaptiveScanPlanner::~AdaptiveScanPlanner() = default;

void AdaptiveScanPlanner::reset()
{
    for (auto& state : impl_->tuningStates) {
        state.consecutiveEmptyVisits = 0;
        state.lastNewRadiosSeen = 0;
        state.lastObservedAt.reset();
        state.lastVisitedAt.reset();
        state.strongestSignalDbm.reset();
    }

    impl_->autoConfig = ScannerAutoConfig{
        .band = ScannerBand::Band5Ghz,
        .widthMhz = kWidth20Mhz,
    };
    impl_->nextTrackingIndex = 0;
    impl_->stepsSinceDiscovery = config_.trackingStepsPerDiscovery;
    impl_->jitterState = config_.rngSeed;
}

void AdaptiveScanPlanner::setAutoConfig(const ScannerAutoConfig& config)
{
    const int effectiveWidthMhz = scannerWidthSupported(config.band, config.widthMhz)
        ? config.widthMhz
        : scannerDefaultWidthMhz(config.band);
    if (impl_->autoConfig.band == config.band && impl_->autoConfig.widthMhz == effectiveWidthMhz) {
        return;
    }

    impl_->autoConfig = ScannerAutoConfig{
        .band = config.band,
        .widthMhz = effectiveWidthMhz,
    };
    impl_->nextTrackingIndex = 0;
    impl_->stepsSinceDiscovery = config_.trackingStepsPerDiscovery;
}

ScanStep AdaptiveScanPlanner::nextStep(const std::chrono::steady_clock::time_point now)
{
    const bool shouldForceDiscovery = impl_->hasOverdueCandidate(now);
    const bool shouldUseDiscovery =
        shouldForceDiscovery || impl_->stepsSinceDiscovery >= config_.trackingStepsPerDiscovery;

    const ScannerTuning tuning =
        shouldUseDiscovery ? impl_->selectDiscoveryTuning(now) : impl_->selectTrackingTuning();
    if (shouldUseDiscovery) {
        impl_->stepsSinceDiscovery = 0;
        impl_->syncTrackingCursorAfterTuning(tuning);
    }
    else {
        ++impl_->stepsSinceDiscovery;
    }

    const int dwellMs = shouldUseDiscovery
        ? impl_->jitteredDwellMs(config_.discoveryDwellMs, config_.discoveryJitterMs)
        : impl_->jitteredDwellMs(config_.trackingDwellMs, config_.trackingJitterMs);
    return ScanStep{
        .tuning = tuning,
        .dwellMs = dwellMs,
    };
}

void AdaptiveScanPlanner::recordObservation(
    const StepObservation& observation, const std::chrono::steady_clock::time_point now)
{
    auto& state = impl_->tuningStateFor(observation.step.tuning);
    state.lastVisitedAt = now;
    if (observation.sawTraffic) {
        state.consecutiveEmptyVisits = 0;
        state.lastNewRadiosSeen = observation.newRadiosSeen;
        state.lastObservedAt = now;
        state.strongestSignalDbm = observation.strongestSignalDbm;
        return;
    }

    ++state.consecutiveEmptyVisits;
    state.lastNewRadiosSeen = 0;
}

} // namespace OsManager
} // namespace DirtSim
