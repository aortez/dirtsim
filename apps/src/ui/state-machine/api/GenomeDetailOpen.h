#pragma once

#include "core/CommandWithCallback.h"
#include "core/Result.h"
#include "core/organisms/evolution/GenomeMetadata.h"
#include "server/api/ApiError.h"
#include "server/api/ApiMacros.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <zpp_bits.h>

namespace DirtSim {
namespace UiApi {

namespace GenomeDetailOpen {

DEFINE_API_NAME(GenomeDetailOpen);

struct Command {
    std::optional<GenomeId> id;
    int index = 0;

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Command fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

struct Okay {
    bool opened = true;
    GenomeId id{};

    API_COMMAND_NAME();
    nlohmann::json toJson() const;
    static Okay fromJson(const nlohmann::json& j);

    using serialize = zpp::bits::members<2>;
};

using OkayType = Okay;
using Response = Result<OkayType, ApiError>;
using Cwc = CommandWithCallback<Command, Response>;

} // namespace GenomeDetailOpen
} // namespace UiApi
} // namespace DirtSim
