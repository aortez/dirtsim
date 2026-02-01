#pragma once

#include "audio/api/AudioApiCommand.h"
#include "core/Result.h"
#include "server/api/ApiError.h"
#include <string>

namespace DirtSim {
namespace AudioProcess {

class CommandDeserializerJson {
public:
    Result<AudioApi::AudioApiCommand, ApiError> deserialize(const std::string& commandJson);
};

} // namespace AudioProcess
} // namespace DirtSim
