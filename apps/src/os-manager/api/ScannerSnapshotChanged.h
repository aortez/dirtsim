#pragma once

#include "ScannerSnapshotGet.h"
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {

struct ScannerSnapshotChanged {
    ScannerSnapshotGet::Okay snapshot;

    static constexpr const char* name() { return "ScannerSnapshotChanged"; }

    using serialize = zpp::bits::members<1>;
};

} // namespace OsApi
} // namespace DirtSim
