#include "AdaptiveScanPlanner.h"
#include <algorithm>
#include <array>
#include <limits>
#include <span>

namespace DirtSim {
namespace OsManager {

namespace {

constexpr int kWidth20Mhz = 20;

constexpr std::array<int, 11> kBand24Channels{ { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
constexpr std::array<int, 9> kBand5Channels{ { 36, 40, 44, 48, 149, 153, 157, 161, 165 } };

std::span<const int> channelsForBand(const ScannerBand band)
{
    switch (band) {
        case ScannerBand::Band24Ghz:
            return kBand24Channels;
        case ScannerBand::Band5Ghz:
            return kBand5Channels;
    }

    return {};
}

ScannerTuning tuningForPrimaryChannel(const int channel)
{
    const auto band = scannerBandFromChannel(channel).value_or(ScannerBand::Band5Ghz);
    return ScannerTuning{
        .band = band,
        .primaryChannel = channel,
        .widthMhz = kWidth20Mhz,
        .centerChannel = std::nullopt,
    };
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
    struct ChannelState {
        int channel = 0;
        ScannerBand band = ScannerBand::Band5Ghz;
        int consecutiveEmptyVisits = 0;
        size_t lastNewRadiosSeen = 0;
        std::optional<std::chrono::steady_clock::time_point> lastObservedAt;
        std::optional<std::chrono::steady_clock::time_point> lastVisitedAt;
        std::optional<int> strongestSignalDbm;
    };

    explicit Impl(const AdaptiveScanPlannerConfig& config) : config(config)
    {
        for (const int channel : kBand24Channels) {
            channelStates.push_back(
                ChannelState{
                    .channel = channel,
                    .band = ScannerBand::Band24Ghz,
                    .consecutiveEmptyVisits = 0,
                    .lastNewRadiosSeen = 0,
                    .lastObservedAt = std::nullopt,
                    .lastVisitedAt = std::nullopt,
                    .strongestSignalDbm = std::nullopt,
                });
        }
        for (const int channel : kBand5Channels) {
            channelStates.push_back(
                ChannelState{
                    .channel = channel,
                    .band = ScannerBand::Band5Ghz,
                    .consecutiveEmptyVisits = 0,
                    .lastNewRadiosSeen = 0,
                    .lastObservedAt = std::nullopt,
                    .lastVisitedAt = std::nullopt,
                    .strongestSignalDbm = std::nullopt,
                });
        }
    }

    ChannelState& channelStateFor(const int channel)
    {
        const auto it = std::find_if(
            channelStates.begin(), channelStates.end(), [channel](const ChannelState& state) {
                return state.channel == channel;
            });
        return *it;
    }

    const ChannelState& channelStateFor(const int channel) const
    {
        const auto it = std::find_if(
            channelStates.begin(), channelStates.end(), [channel](const ChannelState& state) {
                return state.channel == channel;
            });
        return *it;
    }

    bool candidatePreferredForDiscovery(
        const ChannelState& candidate,
        const ChannelState& currentBest,
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

        return candidate.channel < currentBest.channel;
    }

    bool hasOverdueChannel(const std::chrono::steady_clock::time_point now) const
    {
        for (const int channel : channelsForBand(focusBand)) {
            const auto& state = channelStateFor(channel);
            if (!state.lastVisitedAt.has_value()) {
                continue;
            }

            const auto age =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - *state.lastVisitedAt);
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

    int selectDiscoveryChannel(const std::chrono::steady_clock::time_point now) const
    {
        const auto channels = channelsForBand(focusBand);
        int bestChannel = channels.front();
        for (const int channel : channels.subspan(1)) {
            const auto& candidate = channelStateFor(channel);
            const auto& currentBest = channelStateFor(bestChannel);
            if (candidatePreferredForDiscovery(candidate, currentBest, now)) {
                bestChannel = channel;
            }
        }

        return bestChannel;
    }

    int selectTrackingChannel()
    {
        const auto channels = channelsForBand(focusBand);
        size_t& index =
            focusBand == ScannerBand::Band24Ghz ? nextBand24TrackingIndex : nextBand5TrackingIndex;
        const int channel = channels[index % channels.size()];
        index = (index + 1) % channels.size();
        return channel;
    }

    void syncTrackingCursorAfterChannel(const int channel)
    {
        const auto band = scannerBandFromChannel(channel);
        if (!band.has_value()) {
            return;
        }

        const auto channels = channelsForBand(*band);
        const auto it = std::find(channels.begin(), channels.end(), channel);
        if (it == channels.end()) {
            return;
        }

        size_t& index =
            *band == ScannerBand::Band24Ghz ? nextBand24TrackingIndex : nextBand5TrackingIndex;
        index = (static_cast<size_t>(std::distance(channels.begin(), it)) + 1) % channels.size();
    }

    AdaptiveScanPlannerConfig config;
    std::vector<ChannelState> channelStates;
    ScannerBand focusBand = ScannerBand::Band5Ghz;
    size_t nextBand24TrackingIndex = 0;
    size_t nextBand5TrackingIndex = 0;
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
    for (auto& state : impl_->channelStates) {
        state.consecutiveEmptyVisits = 0;
        state.lastNewRadiosSeen = 0;
        state.lastObservedAt.reset();
        state.lastVisitedAt.reset();
        state.strongestSignalDbm.reset();
    }

    impl_->nextBand24TrackingIndex = 0;
    impl_->nextBand5TrackingIndex = 0;
    impl_->stepsSinceDiscovery = config_.trackingStepsPerDiscovery;
    impl_->jitterState = config_.rngSeed;
}

void AdaptiveScanPlanner::setFocusBand(const ScannerBand band)
{
    if (impl_->focusBand == band) {
        return;
    }

    impl_->focusBand = band;
    impl_->stepsSinceDiscovery = config_.trackingStepsPerDiscovery;
}

ScanStep AdaptiveScanPlanner::nextStep(const std::chrono::steady_clock::time_point now)
{
    const bool shouldForceDiscovery = impl_->hasOverdueChannel(now);
    const bool shouldUseDiscovery =
        shouldForceDiscovery || impl_->stepsSinceDiscovery >= config_.trackingStepsPerDiscovery;

    const int channel =
        shouldUseDiscovery ? impl_->selectDiscoveryChannel(now) : impl_->selectTrackingChannel();
    if (shouldUseDiscovery) {
        impl_->stepsSinceDiscovery = 0;
        impl_->syncTrackingCursorAfterChannel(channel);
    }
    else {
        ++impl_->stepsSinceDiscovery;
    }

    const int dwellMs = shouldUseDiscovery
        ? impl_->jitteredDwellMs(config_.discoveryDwellMs, config_.discoveryJitterMs)
        : impl_->jitteredDwellMs(config_.trackingDwellMs, config_.trackingJitterMs);
    return ScanStep{
        .tuning = tuningForPrimaryChannel(channel),
        .dwellMs = dwellMs,
    };
}

void AdaptiveScanPlanner::recordObservation(
    const StepObservation& observation, const std::chrono::steady_clock::time_point now)
{
    auto& state = impl_->channelStateFor(observation.step.tuning.primaryChannel);
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
