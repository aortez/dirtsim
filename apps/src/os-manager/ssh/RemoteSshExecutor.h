#pragma once

#include "core/Result.h"
#include "os-manager/PeerTrust.h"
#include "os-manager/api/RemoteCliRun.h"
#include "server/api/ApiError.h"
#include <cstddef>
#include <filesystem>
#include <vector>

namespace DirtSim {
namespace OsManager {

class RemoteSshExecutor {
public:
    static constexpr size_t kMaxStdoutBytes = 2 * 1024 * 1024;
    static constexpr size_t kMaxStderrBytes = 2 * 1024 * 1024;

    explicit RemoteSshExecutor(std::filesystem::path keyPath);

    Result<OsApi::RemoteCliRun::Okay, ApiError> run(
        const PeerTrustBundle& peer,
        const std::vector<std::string>& argv,
        int commandTimeoutMs) const;

private:
    std::filesystem::path keyPath_;
};

} // namespace OsManager
} // namespace DirtSim
