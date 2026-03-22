#pragma once

#include "core/Result.h"
#include <string>
#include <vector>

namespace DirtSim {
namespace OsManager {

struct ProcessRunResult {
    int exitCode = -1;
    std::string output;
};

Result<ProcessRunResult, std::string> runProcessCapture(
    const std::vector<std::string>& argv, int timeoutMs);

} // namespace OsManager
} // namespace DirtSim
