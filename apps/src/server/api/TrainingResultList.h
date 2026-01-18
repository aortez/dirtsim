#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "TrainingResultAvailable.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"

#include <nlohmann/json.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace TrainingResultList {

DEFINE_API_NAME(TrainingResultList);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    API_COMMAND();
    API_JSON_SERIALIZABLE(Command);

    using serialize = zpp::bits::members<0>;
};

struct Entry {
    TrainingResultAvailable::Summary summary;
    int candidateCount = 0;

    using serialize = zpp::bits::members<2>;
};

void to_json(nlohmann::json& j, const Entry& entry);
void from_json(const nlohmann::json& j, Entry& entry);

struct Okay {
    std::vector<Entry> results;

    API_COMMAND_NAME();
    API_JSON_SERIALIZABLE(Okay);

    using serialize = zpp::bits::members<1>;
};

API_STANDARD_TYPES();

} // namespace TrainingResultList
} // namespace Api
} // namespace DirtSim
