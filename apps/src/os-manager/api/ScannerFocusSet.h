#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "os-manager/ScannerTypes.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {
namespace ScannerFocusSet {

DEFINE_API_NAME(ScannerFocusSet);

struct Okay;

struct Command {
    OsManager::ScannerBand band = OsManager::ScannerBand::Band5Ghz;

    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<1>;
};

struct Okay {
    OsManager::ScannerBand band = OsManager::ScannerBand::Band5Ghz;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace ScannerFocusSet
} // namespace OsApi
} // namespace DirtSim
