#pragma once

#include "core/Result.h"
#include <string>
#include <variant>

namespace DirtSim {
namespace OsManager {

class ScannerChannelController {
public:
    virtual ~ScannerChannelController() = default;

    virtual Result<std::monostate, std::string> start() = 0;
    virtual void stop() = 0;
    virtual Result<std::monostate, std::string> setChannel20MHz(int channel) = 0;
};

} // namespace OsManager
} // namespace DirtSim
