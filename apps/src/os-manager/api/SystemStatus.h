#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace SystemStatus {

DEFINE_API_NAME(SystemStatus);

struct Okay;

struct Command {
    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

struct Okay {
    uint64_t uptime_seconds = 0;
    double cpu_percent = 0.0;
    uint64_t memory_free_kb = 0;
    uint64_t memory_total_kb = 0;
    uint64_t disk_free_bytes_root = 0;
    uint64_t disk_total_bytes_root = 0;
    uint64_t disk_free_bytes_data = 0;
    uint64_t disk_total_bytes_data = 0;
    std::string ui_status;
    std::string server_status;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<10>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace SystemStatus
} // namespace OsApi
} // namespace DirtSim
