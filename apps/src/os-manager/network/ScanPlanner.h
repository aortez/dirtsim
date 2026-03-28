#pragma once

#include "os-manager/ScannerTypes.h"
#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace DirtSim {
namespace OsManager {

struct ScanStep {
    ScannerTuning tuning;
    int dwellMs = 0;
};

struct StepObservation {
    ScanStep step;
    bool sawTraffic = false;
    size_t radiosSeen = 0;
    size_t newRadiosSeen = 0;
    std::optional<int> strongestSignalDbm;
    std::vector<int> observedChannels;
};

class ScanPlanner {
public:
    virtual ~ScanPlanner() = default;

    virtual void reset() = 0;
    virtual void setAutoConfig(const ScannerAutoConfig& config) = 0;
    virtual ScanStep nextStep(std::chrono::steady_clock::time_point now) = 0;
    virtual void recordObservation(
        const StepObservation& observation, std::chrono::steady_clock::time_point now) = 0;
};

} // namespace OsManager
} // namespace DirtSim
