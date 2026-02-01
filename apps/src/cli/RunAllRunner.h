#pragma once

#include "core/Result.h"
#include <string>
#include <variant>

namespace DirtSim {
namespace Client {

/**
 * @brief Launches server, UI, and audio, monitors until UI exits, then shuts down.
 * @param serverPath Path to dirtsim-server executable.
 * @param uiPath Path to dirtsim-ui executable.
 * @param audioPath Path to dirtsim-audio executable.
 * @return Ok on success, error message on failure.
 */
Result<std::monostate, std::string> runAll(
    const std::string& serverPath, const std::string& uiPath, const std::string& audioPath);

} // namespace Client
} // namespace DirtSim
