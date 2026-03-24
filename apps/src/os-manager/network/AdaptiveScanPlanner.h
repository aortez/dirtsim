#pragma once

#include "os-manager/network/ScanPlanner.h"
#include <cstdint>
#include <memory>

namespace DirtSim {
namespace OsManager {

struct AdaptiveScanPlannerConfig {
    int trackingDwellMs = 150;
    int discoveryDwellMs = 250;
    int minDwellMs = 100;
    int maxDwellMs = 300;
    int trackingStepsPerDiscovery = 3;
    int trackingJitterMs = 10;
    int discoveryJitterMs = 20;
    int maxRevisitAgeMs = 3000;
    uint32_t rngSeed = 0x6d2b79f5u;
};

class AdaptiveScanPlanner : public ScanPlanner {
public:
    explicit AdaptiveScanPlanner(AdaptiveScanPlannerConfig config = AdaptiveScanPlannerConfig{});
    ~AdaptiveScanPlanner() override;

    void reset() override;
    void setFocusBand(ScannerBand band) override;
    ScanStep nextStep(std::chrono::steady_clock::time_point now) override;
    void recordObservation(
        const StepObservation& observation, std::chrono::steady_clock::time_point now) override;

private:
    struct Impl;

    AdaptiveScanPlannerConfig config_;
    std::unique_ptr<Impl> impl_;
};

} // namespace OsManager
} // namespace DirtSim
