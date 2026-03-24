#pragma once

#include "ScannerChannelController.h"
#include <mutex>
#include <vector>

namespace DirtSim {
namespace OsManager {

class NexmonChannelController : public ScannerChannelController {
public:
    NexmonChannelController() = default;
    ~NexmonChannelController() override;

    Result<std::monostate, std::string> start() override;
    void stop() override;
    Result<std::monostate, std::string> setTuning(const ScannerTuning& tuning) override;

private:
    Result<std::vector<uint8_t>, std::string> transact(const std::vector<uint8_t>& payload);

    std::mutex mutex_;
    int rxSocketFd_ = -1;
    int txSocketFd_ = -1;
};

} // namespace OsManager
} // namespace DirtSim
