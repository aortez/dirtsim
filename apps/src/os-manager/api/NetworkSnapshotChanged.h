#pragma once

#include "NetworkSnapshotGet.h"
#include <zpp_bits.h>

namespace DirtSim {
namespace OsApi {

struct NetworkSnapshotChanged {
    NetworkSnapshotGet::Okay snapshot;

    static constexpr const char* name() { return "NetworkSnapshotChanged"; }

    using serialize = zpp::bits::members<1>;
};

} // namespace OsApi
} // namespace DirtSim
