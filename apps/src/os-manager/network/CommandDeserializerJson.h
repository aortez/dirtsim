#pragma once

#include "core/Result.h"
#include "os-manager/api/OsApiCommand.h"
#include "server/api/ApiError.h"
#include <string>

namespace DirtSim {
namespace OsManager {

class CommandDeserializerJson {
public:
    Result<OsApi::OsApiCommand, ApiError> deserialize(const std::string& commandJson);
};

} // namespace OsManager
} // namespace DirtSim
