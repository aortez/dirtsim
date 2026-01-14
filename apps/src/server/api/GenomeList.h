#pragma once

#include "ApiError.h"
#include "ApiMacros.h"
#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <zpp_bits.h>

namespace DirtSim {
namespace Api {

namespace GenomeList {

DEFINE_API_NAME(GenomeList);

struct Okay; // Forward declaration for API_COMMAND() macro.

struct Command {
    API_COMMAND();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<0>;
};

struct GenomeEntry {
    GenomeId id{};
    GenomeMetadata metadata;

    using serialize = zpp::bits::members<2>;
};

void to_json(nlohmann::json& j, const GenomeEntry& e);
void from_json(const nlohmann::json& j, GenomeEntry& e);

struct Okay {
    std::vector<GenomeEntry> genomes;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;

    using serialize = zpp::bits::members<1>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace GenomeList
} // namespace Api
} // namespace DirtSim
