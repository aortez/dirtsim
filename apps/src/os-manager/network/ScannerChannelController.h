#pragma once

#include "core/Result.h"
#include "os-manager/ScannerTypes.h"
#include <string>
#include <variant>

namespace DirtSim {
namespace OsManager {

class ScannerChannelController {
public:
    virtual ~ScannerChannelController() = default;

    virtual Result<std::monostate, std::string> start() = 0;
    virtual void stop() = 0;
    virtual Result<std::monostate, std::string> setTuning(const ScannerTuning& tuning) = 0;
};

} // namespace OsManager
} // namespace DirtSim
